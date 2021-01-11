#include "../Includes/USBReader.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <sys/types.h>

#include <libusb.h>
#include <unistd.h>

#include "../Includes/HostFS.h"
#include "../Includes/Logger.h"
#include "../Includes/NetConversionFunctions.h"
#include "../Includes/XLinkKaiConnection.h"

#if defined(_MSC_VER) && defined(__MINGW32__)
#include <hidclass.h>
#endif

namespace
{
    namespace eUSBResetStatus
    {
        enum Status
        {
            Closing,
            Resetting,
            Opening,
            Opened,
        };
    }  // namespace eUSBResetStatus

    namespace eUSBResetDeviceStatus
    {
        enum Status
        {
            None,
            Resetting,
        };
    }  // namespace eUSBResetDeviceStatus

    const std::vector<std::string_view> cUsbResetStatus{
        "USB_RESET_STATUS_CLOSING",
        "USB_RESET_STATUS_RESETTING",
        "USB_RESET_STATUS_OPENING",
        "USB_RESET_STATUS_OPENED",
    };

    constexpr int cPSPVID = 0x54C;
    constexpr int cPSPPID = 0x1C9;
    constexpr int cMaxRetries{5};

}  // namespace

using namespace std::chrono_literals;

bool USBReader::OpenDevice()
{
    libusb_device_handle* lDeviceHandle{nullptr};
    libusb_device**       lDevices{nullptr};
    libusb_device*        lDevice{nullptr};
    int                   lCount{0};
    int                   lReturn{0};

    libusb_init(nullptr);

    lReturn = libusb_get_device_list(nullptr, &lDevices);
    if (lReturn >= 0) {
        for (lCount = 0; lDevices[lCount] != nullptr; lCount++) {
            lDevice = lDevices[lCount];
            libusb_device_descriptor lDescriptor{};
            lReturn = libusb_get_device_descriptor(lDevice, &lDescriptor);

            if (lReturn >= 0) {
                if ((lDescriptor.idVendor == cPSPVID) && (lDescriptor.idProduct == cPSPPID)) {
                    lReturn = libusb_open(lDevice, &lDeviceHandle);
                    if (lReturn >= 0 && lDeviceHandle != nullptr) {
                        lReturn = libusb_set_configuration(lDeviceHandle, 1);

                        if (lReturn >= 0) {
                            lReturn = libusb_claim_interface(lDeviceHandle, 0);
                            if (lReturn == 0) {
                                mDeviceHandle = lDeviceHandle;
                            } else {
                                Logger::GetInstance().Log(
                                    std::string("Could not detach kernel driver: ") + libusb_strerror(lReturn),
                                    Logger::Level::ERROR);
                                libusb_close(lDeviceHandle);
                            }
                        } else {
                            Logger::GetInstance().Log(
                                std::string("Could set configuration: ") + libusb_strerror(lReturn),
                                Logger::Level::ERROR);
                            libusb_close(lDeviceHandle);
                        }
                    } else {
                        Logger::GetInstance().Log(std::string("Could not open USB device") + libusb_strerror(lReturn),
                                                  Logger::Level::ERROR);
                    }
                } else {
                    std::stringstream lVidPid;
                    lVidPid << std::hex << std::setfill('0') << std::setw(4) << lDescriptor.idVendor << ":" << std::hex
                            << std::setfill('0') << std::setw(4) << lDescriptor.idProduct;
                    Logger::GetInstance().Log(std::string("Non matching device found: ") + lVidPid.str(),
                                              Logger::Level::TRACE);
                }
            } else {
                Logger::GetInstance().Log(std::string("Cannot query device descriptor") + libusb_strerror(lReturn),
                                          Logger::Level::ERROR);
                libusb_free_device_list(lDevices, 1);
            }
        }
    } else {
        Logger::GetInstance().Log(std::string("Could not get device list: ") + libusb_strerror(lReturn),
                                  Logger::Level::ERROR);
    }

    return (mDeviceHandle != nullptr);
}


void USBReader::CloseDevice()
{
    if (mDeviceHandle != nullptr) {
        libusb_release_interface(mDeviceHandle, 0);
        libusb_reset_device(mDeviceHandle);
        libusb_close(mDeviceHandle);
        mDeviceHandle = nullptr;
        libusb_exit(nullptr);
    }
}


int USBReader::USBBulkWrite(int aEndpoint, std::string_view aData, int aSize, int aTimeOut)
{
    int lReturn{-1};

    if (mDeviceHandle != nullptr) {
        Logger::GetInstance().Log(
            std::string("Bulk Write, size: ") + std::to_string(aSize) + " , timeout: " + std::to_string(aTimeOut),
            Logger::Level::ERROR);

        // Copy to a vector temporarily so we have a non-const unsigned char array
        std::vector<unsigned char> lString{aData.data(), aData.data() + aSize};
        int mError = libusb_bulk_transfer(mDeviceHandle, aEndpoint, lString.data(), aSize, &lReturn, aTimeOut);
        if (mError >= 0) {
            Logger::GetInstance().Log(std::string("Bulk write returned: ") + std::to_string(lReturn),
                                      Logger::Level::TRACE);
        } else {
            Logger::GetInstance().Log(std::string("Error during Bulk write: ") + libusb_strerror(mError),
                                      Logger::Level::ERROR);
            lReturn = -1;
        }
    }

    return lReturn;
}

int USBReader::USBBulkRead(int aEndpoint, int aSize, int aTimeOut)
{
    int lReturn{-1};
    if (mDeviceHandle != nullptr) {
        int mError = libusb_bulk_transfer(mDeviceHandle, aEndpoint, mBuffer.data(), aSize, &lReturn, aTimeOut);
        try {
            Logger::GetInstance().Log(
                std::string("Bulk Read, size: ") + std::to_string(aSize) + " , timeout: " + std::to_string(aTimeOut) +
                    ", data: " + PrettyHexString(std::string(reinterpret_cast<char*>(mBuffer.data()), lReturn)),
                Logger::Level::TRACE);

            Logger::GetInstance().Log(std::string("Bulk read returned: ") + std::to_string(lReturn),
                                      Logger::Level::ERROR);
        } catch (const std::exception& aException) {
            // Size changed when reading, possible that the PSP disconnected while transferring
            lReturn = -1;
        }

        if (mError != 0) {
            lReturn = mError;
        }
    }

    return lReturn;
}

void USBReader::SetIncomingConnection(std::shared_ptr<XLinkKaiConnection> aDevice)
{
    mIncomingConnection = aDevice;
}


int USBReader::SendHello()
{
    HostFS_Constants::HostFsCommand lResponse{};
    memset(&lResponse, 0, sizeof(HostFS_Constants::HostFsCommand));

    lResponse.magic   = HostFS_Constants::HostFS;
    lResponse.command = HostFS_Constants::Hello;
    Logger::GetInstance().Log(PrettyHexString(std::string(reinterpret_cast<char*>(&lResponse), 12)),
                              Logger::Level::TRACE);

    return USBBulkWrite(0x2, reinterpret_cast<char*>(&lResponse), sizeof(HostFS_Constants::HostFsCommand), 10000);
}

bool USBReader::USBCheckDevice()
{
    bool lReturn{true};
    Logger::GetInstance().Log("USBCheckDevice", Logger::Level::TRACE);

    if (mDeviceHandle != nullptr) {
        int lMagic  = HostFS_Constants::HostFS;
        int lLength = USBBulkWrite(2, reinterpret_cast<char*>(&lMagic), sizeof(int), 1000);
        if (lLength != sizeof(int)) {
            Logger::GetInstance().Log(std::string("Amount of bytes written did not match: ") + std::to_string(lLength),
                                      Logger::Level::WARNING);
            lReturn = false;
        }
    }
    return lReturn;
}

void USBReader::ReceiveCallback()
{
    int   lLength{mLength};
    auto* lCommand{reinterpret_cast<HostFS_Constants::HostFsCommand*>(mBuffer.data())};
    switch (static_cast<HostFS_Constants::eMagicType>(lCommand->magic)) {
        case HostFS_Constants::HostFS:
            if (mLength >= sizeof(HostFS_Constants::HostFsCommand)) {
                if (lCommand->command == (HostFS_Constants::Hello)) {
                    SendHello();
                } else {
                    mError = true;
                    Logger::GetInstance().Log("PSP is being rude and not sending a Hello back :V. Disconnecting!" +
                                                  std::to_string(lCommand->command),
                                              Logger::Level::ERROR);
                }
                std::this_thread::sleep_for(100ms);
            }
            break;
        case HostFS_Constants::Asynchronous:
            if (lLength > sizeof(HostFS_Constants::AsyncCommand)) {
                // This is actually the channel, for now we'll capture all
                if (lCommand->command == 4) {
                    if (lLength > sizeof(HostFS_Constants::AsyncCommand) + sizeof(HostFS_Constants::ScreenHeader)) {
                        lLength -= sizeof(HostFS_Constants::AsyncCommand);
                        auto* lEvent{reinterpret_cast<HostFS_Constants::ScreenHeader*>(
                            mBuffer.data() + sizeof(HostFS_Constants::AsyncCommand))};

                        if (lEvent->magic == HostFS_Constants::DebugPrint) {
                            if (lEvent->mode == HostFS_Constants::cAsyncCommandPacket &&
                                lEvent->ref == HostFS_Constants::cAsyncCommandSendPacket) {
                                std::string lData{
                                    reinterpret_cast<char*>(lEvent) + sizeof(HostFS_Constants::ScreenHeader),
                                    lLength - sizeof(HostFS_Constants::ScreenHeader)};

                                mIncomingConnection->Send(lData);

                                Logger::GetInstance().Log("Data: " + PrettyHexString(lData), Logger::Level::TRACE);

                            } else {
                                Logger::GetInstance().Log(
                                    "PSP: " +
                                        PrettyHexString(std::string(
                                            reinterpret_cast<char*>(lEvent) + sizeof(HostFS_Constants::ScreenHeader),
                                            lLength - sizeof(HostFS_Constants::ScreenHeader))),
                                    Logger::Level::INFO);
                            }
                        }
                    }
                } else {
                    // Don't know what we got
                    char* lData = reinterpret_cast<char*>(mBuffer.data()) + sizeof(HostFS_Constants::AsyncCommand);
                    Logger::GetInstance().Log(
                        "Unknown Data: " + std::string(lData, mLength - sizeof(HostFS_Constants::AsyncCommand)),
                        Logger::Level::TRACE);
                }
            } else {
                Logger::GetInstance().Log("Packet received too small???", Logger::Level::ERROR);
            }
            break;
        case HostFS_Constants::Bulk:
            Logger::GetInstance().Log("Bulk received, weird", Logger::Level::DEBUG);
        default:
            Logger::GetInstance().Log("Magic not recognized: " + std::to_string(lCommand->magic), Logger::Level::DEBUG);
            break;
    }
    mLength = 0;
}

bool USBReader::StartReceiverThread()
{
    bool lReturn{true};

    if (mDeviceHandle != nullptr) {
        if (mReceiverThread == nullptr && USBCheckDevice()) {
            mReceiverThread = std::make_shared<boost::thread>([&] {
                // If we didn't get a graceful disconnect retry making connection.
                while ((mDeviceHandle != nullptr) || mRetryCounter > 0) {
                    if (mError && mRetryCounter < cMaxRetries) {
                        // Do a full reset
                        CloseDevice();
                        OpenDevice();
                        mRetryCounter++;
                        mError = false;
                    } else if (mRetryCounter >= cMaxRetries) {
                        Logger::GetInstance().Log("Too many errors! Bailing out", Logger::Level::ERROR);
                        CloseDevice();
                        mRetryCounter = 0;
                    }
                    int lLength{USBBulkRead(0x81, 512, 1000)};
                    if (lLength > 0) {
                        mLength = lLength;
                        Logger::GetInstance().Log("Callback", Logger::Level::TRACE);
                        ReceiveCallback();
                    } else if (lLength == LIBUSB_ERROR_TIMEOUT) {
                        // Timeout errors are probably recoverable
                        std::this_thread::sleep_for(100ms);
                    } else if (lLength != LIBUSB_ERROR_BUSY) {
                        // Also not fatal probably, wait another 10ms
                        std::this_thread::sleep_for(10ms);
                    } else {
                        // Probably fatal, try a restart of the device
                        mError = true;
                    }
                    std::this_thread::sleep_for(1ms);
                }
            });
        } else {
            lReturn = false;
        }
    } else {
        lReturn = false;
    }
    return lReturn;
}

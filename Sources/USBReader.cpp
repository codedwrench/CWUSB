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
    constexpr int cMaxRetries{1000};

}  // namespace

using namespace std::chrono_literals;

/**
 * Checks if command is a debugprint command with the given mode.
 * @param aData - Data to check.
 * @param aLength - Length of data to check.
 * @return number of debugprint command if it is a debugprint command
 */
static inline int IsDebugPrintCommand(HostFS_Constants::AsyncCommand& aData, int aLength)
{
    int lReturn{0};
    int lLength{aLength};
    // Check if it has a subheader
    if (lLength > sizeof(HostFS_Constants::AsyncCommand) + sizeof(HostFS_Constants::AsyncSubHeader)) {
        lLength -= sizeof(HostFS_Constants::AsyncCommand);
        auto* lSubHeader{reinterpret_cast<HostFS_Constants::AsyncSubHeader*>(&aData) +
                         sizeof(HostFS_Constants::AsyncCommand)};
        if (lSubHeader->magic == HostFS_Constants::DebugPrint && lSubHeader->size == aLength) {
            if (lSubHeader->mode == HostFS_Constants::cAsyncModePacket &&
                lSubHeader->ref == HostFS_Constants::cAsyncCommandSendPacket) {
                lReturn = HostFS_Constants::cAsyncModePacket;
            } else if (lSubHeader->mode == HostFS_Constants::cAsyncModeDebug &&
                       lSubHeader->ref == HostFS_Constants::cAsyncCommandPrintData) {
                lReturn = HostFS_Constants::cAsyncModeDebug;
            }
        }
    }
    return lReturn;
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

void USBReader::HandleAsynchronous(HostFS_Constants::AsyncCommand& aData, int aLength)
{
    // This is actually the channel, for now we'll capture all
    if (aData.channel == HostFS_Constants::cAsyncUserChannel) {
        if (mStitching) {
            HandleStitch(aData, aLength);
        } else {
            HandleAsynchronousData(aData, aLength);
        }
    } else {
        // Don't know what we got
        char* lData{reinterpret_cast<char*>(&aData)};
        Logger::GetInstance().Log(
            "Unknown data:" + PrettyHexString(std::string(lData, mLength - sizeof(HostFS_Constants::AsyncCommand))),
            Logger::Level::DEBUG);
    }
}

void USBReader::HandleAsynchronousData(HostFS_Constants::AsyncCommand& aData, int aLength)
{
    int lPacketMode{IsDebugPrintCommand(aData, aLength)};

    if (lPacketMode > 0) {
        // We know it's a DebugPrint command, so we can skip past this header as well now
        unsigned int lLength =
            aLength -
            (static_cast<int>(sizeof(HostFS_Constants::AsyncCommand) + sizeof(HostFS_Constants::AsyncSubHeader)));

        std::string lData{reinterpret_cast<char*>(&aData) + sizeof(HostFS_Constants::AsyncCommand) +
                              sizeof(HostFS_Constants::AsyncSubHeader),
                          static_cast<size_t>(lLength)};

        // We are a packet, so we can check if we can send it off
        if (lPacketMode == HostFS_Constants::cAsyncModePacket) {
            // It fits within the standard USB buffer, nothing special needs to be done!
            if (aLength < HostFS_Constants::cMaxUSBPacketSize) {
                mIncomingConnection->Send(lData);

            } else {
                // It does not fit within the standard USB buffer, let's get stitchin'!
                mStitching = true;
                lData.copy(static_cast<char*>(mAsyncBuffer.data()), lLength, 0);

                // Not sure if std::array::size() will be reliable when we will the thing
                // with zeroes
                mBytesInAsyncBuffer = lLength;
            }
        } else if (lPacketMode == HostFS_Constants::cAsyncModeDebug) {
            // We can just go ahead and print the debug data, I'm assuming it will never go past 512 bytes. If it does,
            // we'll see when we get there :|
            Logger::GetInstance().Log("PSP: " + std::string(lData, mLength - sizeof(HostFS_Constants::AsyncCommand)),
                                      Logger::Level::DEBUG);
        } else {
            // Don't know what we got
            char* lData{reinterpret_cast<char*>(&aData)};
            Logger::GetInstance().Log(
                "Unknown data:" + PrettyHexString(std::string(lData, mLength - sizeof(HostFS_Constants::AsyncCommand))),
                Logger::Level::DEBUG);
        }
    } else {
        // Don't know what we got
        char* lData{reinterpret_cast<char*>(&aData)};
        Logger::GetInstance().Log(
            "Unknown data:" + PrettyHexString(std::string(lData, mLength - sizeof(HostFS_Constants::AsyncCommand))),
            Logger::Level::DEBUG);
    }
}

void USBReader::HandleStitch(HostFS_Constants::AsyncCommand& aData, int aLength)
{
    int lLength{aLength};
    if (IsDebugPrintCommand(aData, aLength) > 0) {
        // We somehow got another packet type before stitching was done probably, maybe the packet was done and
        // happened to be 512 bytes long, send it
        mIncomingConnection->Send(std::string(mAsyncBuffer.data(), mBytesInAsyncBuffer));
        mBytesInAsyncBuffer = 0;
        mStitching          = false;

        // Now on to the current packet, lets just call HandleAsynchronousData, because this is the job of that
        // function anyway
        HandleAsynchronousData(aData, aLength);

        Logger::GetInstance().Log("We got a debugprint command while stitching, solved as best as we could",
                                  Logger::Level::TRACE);

        // Donezo :D
    } else {
        // Lets continue stitching
        // If we are stitching, the size should be bigger than the command
        if (lLength > sizeof(HostFS_Constants::AsyncCommand)) {
            // Focus on the data part
            std::string lData{reinterpret_cast<char*>(&aData) + sizeof(HostFS_Constants::AsyncCommand),
                              lLength - sizeof(HostFS_Constants::AsyncCommand)};

            lData.copy(mAsyncBuffer.data() + mBytesInAsyncBuffer, 0);
            mBytesInAsyncBuffer += lLength - static_cast<int>(sizeof(HostFS_Constants::AsyncCommand));

            // If we are smaller than the max USB packet size, we are done and we can send the packet on its merry way
            if (lLength < HostFS_Constants::cMaxUSBPacketSize) {
                // This can be sent to XLink Kai
                mIncomingConnection->Send(std::string(mAsyncBuffer.data(), mBytesInAsyncBuffer));
                mBytesInAsyncBuffer = 0;
                mStitching          = false;
            }
        } else {
            // Don't know what we got
            char* lData{reinterpret_cast<char*>(&aData)};
            Logger::GetInstance().Log(
                "Unknown data:" + PrettyHexString(std::string(lData, mLength - sizeof(HostFS_Constants::AsyncCommand))),
                Logger::Level::DEBUG);
        }
    }
}

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

void USBReader::SetIncomingConnection(std::shared_ptr<XLinkKaiConnection> aDevice)
{
    mIncomingConnection = aDevice;
}

int USBReader::USBBulkRead(int aEndpoint, int aSize, int aTimeOut)
{
    int lReturn{-1};
    if (mDeviceHandle != nullptr) {
        int lError = libusb_bulk_transfer(
            mDeviceHandle, aEndpoint, reinterpret_cast<unsigned char*>(mBuffer.data()), aSize, &lReturn, aTimeOut);
        try {
            Logger::GetInstance().Log(
                std::string("Bulk Read, size: ") + std::to_string(aSize) + " , timeout: " + std::to_string(aTimeOut) +
                    ", data: " + PrettyHexString(std::string(reinterpret_cast<char*>(mBuffer.data()), lReturn)),
                Logger::Level::TRACE);
        } catch (const std::exception& aException) {
            // Size changed when reading, possible that the PSP disconnected while transferring
            lReturn = -1;
        }

        if (lError != 0) {
            lReturn = lError;
        }
    }

    return lReturn;
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
        int lError = libusb_bulk_transfer(mDeviceHandle, aEndpoint, lString.data(), aSize, &lReturn, aTimeOut);
        if (lError < 0) {
            Logger::GetInstance().Log(std::string("Error during Bulk write: ") + libusb_strerror(mError),
                                      Logger::Level::ERROR);
            lReturn = -1;
        }
    }

    return lReturn;
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
    int lLength{mLength};

    // Length should be atleast the size of a command header
    if (lLength >= sizeof(HostFS_Constants::HostFsCommand)) {
        auto* lCommand{reinterpret_cast<HostFS_Constants::HostFsCommand*>(mBuffer.data())};
        switch (static_cast<HostFS_Constants::eMagicType>(lCommand->magic)) {
            case HostFS_Constants::HostFS:
                if (lCommand->command == (HostFS_Constants::Hello)) {
                    SendHello();
                } else {
                    mError = true;
                    Logger::GetInstance().Log("PSP is being rude and not sending a Hello back :V. Disconnecting!" +
                                                  std::to_string(lCommand->command),
                                              Logger::Level::ERROR);
                }
                std::this_thread::sleep_for(100ms);
                break;
            case HostFS_Constants::Asynchronous:
                // We know it's asynchronous data now
                HandleAsynchronous(*reinterpret_cast<HostFS_Constants::AsyncCommand*>(lCommand), lLength);
                break;
            case HostFS_Constants::Bulk:
                Logger::GetInstance().Log("Bulk received, weird", Logger::Level::DEBUG);
            default:
                Logger::GetInstance().Log("Magic not recognized: " + std::to_string(lCommand->magic),
                                          Logger::Level::DEBUG);
                break;
        }
    } else {
        Logger::GetInstance().Log("Packet too short to be usable", Logger::Level::DEBUG);
    }
    mLength = 0;
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

bool USBReader::StartReceiverThread()
{
    bool lReturn{true};

    if (mDeviceHandle != nullptr) {
        if (mReceiverThread == nullptr) {
            mReceiverThread = std::make_shared<boost::thread>([&] {
                // If we didn't get a graceful disconnect retry making connection.
                while ((mDeviceHandle != nullptr) || mRetryCounter > 0) {
                    if (!mUSBCheckSuccessful) {
                        mUSBCheckSuccessful = USBCheckDevice();
                    }
                    if ((mError && mRetryCounter < cMaxRetries) || (!mUSBCheckSuccessful)) {
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
                    // Very small delay to make the computer happy
                    std::this_thread::sleep_for(10us);
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

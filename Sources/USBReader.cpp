#include "../Includes/USBReader.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Not needed for this file but xlinkconnection needs it and this solves an ordering issue on windows
#include <boost/asio.hpp>
#include <sys/types.h>

#include <libusb.h>

#include "../Includes/Logger.h"
#include "../Includes/NetConversionFunctions.h"
#include "../Includes/XLinkKaiConnection.h"

#if defined(_MSC_VER) && defined(__MINGW32__)
#include <hidclass.h>
#endif

namespace
{
    constexpr unsigned int cPSPVID{0x54C};
    constexpr unsigned int cPSPPID{0x1C9};
    constexpr unsigned int cMaxRetries{50};
    constexpr unsigned int cMaxSendBufferItems{100};


}  // namespace

using namespace std::chrono_literals;
using namespace USB_Constants;

/**
 * Checks if command is a debugprint command with the given mode.
 * @param aData - Data to check.
 * @param aLength - Length of data to check.
 * @return number of debugprint command if it is a debugprint command
 */
static inline int IsDebugPrintCommand(AsyncCommand& aData, int aLength)
{
    int lReturn{0};
    int lLength{aLength};
    // Check if it has a subheader
    if (lLength > cAsyncHeaderAndSubHeaderSize) {
        Logger::GetInstance().Log("Size of packet:" + std::to_string(aLength), Logger::Level::TRACE);
        lLength -= cAsyncHeaderSize;
        auto* lSubHeader{reinterpret_cast<AsyncSubHeader*>(reinterpret_cast<char*>(&aData) + cAsyncHeaderSize)};
        if (lSubHeader->magic == DebugPrint) {
            if (lSubHeader->mode == cAsyncModePacket && lSubHeader->ref == cAsyncCommandSendPacket) {
                Logger::GetInstance().Log("Size reported:" + std::to_string(lSubHeader->size), Logger::Level::TRACE);
                lReturn = cAsyncModePacket;
            } else if (lSubHeader->mode == cAsyncModeDebug && lSubHeader->ref == cAsyncCommandPrintData) {
                lReturn = cAsyncModeDebug;
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

void USBReader::HandleAsynchronous(AsyncCommand& aData, int aLength)
{
    // This is actually the channel, for now we'll capture all
    if (aData.channel == cAsyncUserChannel) {
        if (mReceiveStitching) {
            HandleStitch(aData, aLength);
        } else {
            HandleAsynchronousData(aData, aLength);
        }
    } else {
        // Don't know what we got
        char* lData{reinterpret_cast<char*>(&aData)};
        Logger::GetInstance().Log("Unknown data:" + PrettyHexString(std::string(lData, mLength)), Logger::Level::DEBUG);
    }
}

void USBReader::HandleAsynchronousData(AsyncCommand& aData, int aLength)
{
    int lPacketMode{IsDebugPrintCommand(aData, aLength)};

    if (lPacketMode > 0) {
        // We know it's a DebugPrint command, so we can skip past this header as well now
        unsigned int lLength = aLength - cAsyncHeaderAndSubHeaderSize;

        std::string lData{reinterpret_cast<char*>(&aData) + cAsyncHeaderAndSubHeaderSize, static_cast<size_t>(lLength)};

        // We are a packet, so we can check if we can send it off
        if (lPacketMode == cAsyncModePacket) {
            // Grab the packet length from the packet
            mActualPacketLength =
                (reinterpret_cast<AsyncSubHeader*>(reinterpret_cast<char*>(&aData) + cAsyncHeaderSize))->size;

            // It fits within the standard USB buffer, nothing special needs to be done!
            if (mActualPacketLength <= cMaxUSBPacketSize - cAsyncHeaderAndSubHeaderSize) {
                mIncomingConnection->Send(lData);

            } else {
                // It does not fit within the standard USB buffer, let's get stitchin'!
                mReceiveStitching = true;
                lData.copy(static_cast<char*>(mAsyncReceiveBuffer.data()), lLength, 0);

                // Not sure if std::array::size() will be reliable when we will the thing
                // with zeroes
                mBytesInAsyncBuffer = lLength;
            }
        } else if (lPacketMode == cAsyncModeDebug) {
            // We can just go ahead and print the debug data, I'm assuming it will never go past 512 bytes. If it does,
            // we'll see when we get there :|
            Logger::GetInstance().Log("PSP: " + std::string(lData, lLength), Logger::Level::DEBUG);
        } else {
            // Don't know what we got
            char* lData{reinterpret_cast<char*>(&aData)};
            Logger::GetInstance().Log("Unknown data:" + PrettyHexString(std::string(lData, mLength)),
                                      Logger::Level::DEBUG);
        }
    } else {
        // Don't know what we got
        char* lData{reinterpret_cast<char*>(&aData)};
        Logger::GetInstance().Log("Unknown data:" + PrettyHexString(std::string(lData, mLength)), Logger::Level::DEBUG);
    }
}

void USBReader::HandleStitch(AsyncCommand& aData, int aLength)
{
    int lLength{aLength};
    if (IsDebugPrintCommand(aData, aLength) > 0) {
        // We somehow got another packet type before stitching was done probably, maybe the packet was done and
        // happened to be 512 bytes long, send it
        mIncomingConnection->Send(std::string(mAsyncReceiveBuffer.data(), mBytesInAsyncBuffer));
        mBytesInAsyncBuffer = 0;
        mReceiveStitching   = false;

        // Now on to the current packet, lets just call HandleAsynchronousData, because this is the job of that
        // function anyway
        HandleAsynchronousData(aData, aLength);

        Logger::GetInstance().Log("We got a debugprint command while stitching, solved as best as we could",
                                  Logger::Level::TRACE);

        // Donezo :D
    } else {
        // Lets continue stitching
        // If we are stitching, the size should be bigger than the command
        if (lLength > cAsyncHeaderSize) {
            // Focus on the data part
            std::string lData{reinterpret_cast<char*>(&aData) + cAsyncHeaderSize, lLength - cAsyncHeaderSize};

            lData.copy(mAsyncReceiveBuffer.data() + mBytesInAsyncBuffer, 0);
            mBytesInAsyncBuffer += lLength - static_cast<int>(cAsyncHeaderSize);

            // If we are smaller than the max USB packet size, we are done and we can send the packet on its merry way
            if (mBytesInAsyncBuffer >= mActualPacketLength) {
                // This can be sent to XLink Kai
                mIncomingConnection->Send(std::string(mAsyncReceiveBuffer.data(), mBytesInAsyncBuffer));
                mBytesInAsyncBuffer = 0;
                mReceiveStitching   = false;
            }
        } else {
            // Don't know what we got
            char* lData{reinterpret_cast<char*>(&aData)};
            Logger::GetInstance().Log("Unknown data:" + PrettyHexString(std::string(lData, mLength)),
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
                    libusb_free_device_list(lDevices, 1);
                    if (lReturn >= 0 && lDeviceHandle != nullptr) {
                        libusb_set_auto_detach_kernel_driver(lDeviceHandle, 1);
                        lReturn = libusb_set_configuration(lDeviceHandle, 1);

                        if (lReturn >= 0) {
                            lReturn = libusb_claim_interface(lDeviceHandle, 0);
                            if (lReturn == 0) {
                                mDeviceHandle = lDeviceHandle;
                            } else {
                                Logger::GetInstance().Log(std::string("Could not detach kernel driver: ") +
                                                              libusb_strerror(static_cast<libusb_error>(lReturn)),
                                                          Logger::Level::ERROR);
                                libusb_close(lDeviceHandle);
                            }
                        } else {
                            Logger::GetInstance().Log(std::string("Could set configuration: ") +
                                                          libusb_strerror(static_cast<libusb_error>(lReturn)),
                                                      Logger::Level::ERROR);
                            libusb_close(lDeviceHandle);
                        }
                    } else {
                        Logger::GetInstance().Log(std::string("Could not open USB device") +
                                                      libusb_strerror(static_cast<libusb_error>(lReturn)),
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
                Logger::GetInstance().Log(
                    std::string("Cannot query device descriptor") + libusb_strerror(static_cast<libusb_error>(lReturn)),
                    Logger::Level::ERROR);
                libusb_free_device_list(lDevices, 1);
            }
        }
    } else {
        Logger::GetInstance().Log(
            std::string("Could not get device list: ") + libusb_strerror(static_cast<libusb_error>(lReturn)),
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
        int lError = libusb_bulk_transfer(mDeviceHandle,
                                          aEndpoint,
                                          reinterpret_cast<unsigned char*>(mTemporaryReceiveBuffer.data()),
                                          aSize,
                                          &lReturn,
                                          aTimeOut);
        try {
            if (lReturn > 0) {
                Logger::GetInstance().Log(
                    std::string("Bulk Read, size: ") + std::to_string(aSize) +
                        " , timeout: " + std::to_string(aTimeOut) + ", data: " +
                        PrettyHexString(std::string(reinterpret_cast<char*>(mTemporaryReceiveBuffer.data()), lReturn)),
                    Logger::Level::TRACE);
            }
        } catch (const std::exception& aException) {
            // Size changed when reading, possible that the PSP disconnected while transferring
            lReturn = -1;
            Logger::GetInstance().Log("Lost connection with PSP", Logger::Level::WARNING);
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
            Logger::Level::TRACE);

        // Copy to a vector temporarily so we have a non-const unsigned char array
        std::vector<unsigned char> lString{aData.data(), aData.data() + aSize};
        int lError = libusb_bulk_transfer(mDeviceHandle, aEndpoint, lString.data(), aSize, &lReturn, aTimeOut);
        if (lError < 0) {
            Logger::GetInstance().Log(
                std::string("Error during Bulk write: ") + libusb_strerror(static_cast<libusb_error>(mError)),
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
        int lMagic  = HostFS;
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
    if (lLength >= cHostFSHeaderSize) {
        auto* lCommand{reinterpret_cast<HostFsCommand*>(mTemporaryReceiveBuffer.data())};
        switch (static_cast<eMagicType>(lCommand->magic)) {
            case HostFS:
                if (lCommand->command == (Hello)) {
                    SendHello();
                } else {
                    mError = true;
                    Logger::GetInstance().Log("PSP is being rude and not sending a Hello back :V. Disconnecting!" +
                                                  std::to_string(lCommand->command),
                                              Logger::Level::ERROR);
                }
                std::this_thread::sleep_for(100ms);
                break;
            case Asynchronous:
                // We know it's asynchronous data now
                HandleAsynchronous(*reinterpret_cast<AsyncCommand*>(lCommand), lLength);
                break;
            case Bulk:
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
    HostFsCommand lResponse{};
    memset(&lResponse, 0, cHostFSHeaderSize);

    lResponse.magic   = HostFS;
    lResponse.command = Hello;
    Logger::GetInstance().Log(PrettyHexString(std::string(reinterpret_cast<char*>(&lResponse), 12)),
                              Logger::Level::TRACE);

    return USBBulkWrite(0x2, reinterpret_cast<char*>(&lResponse), cHostFSHeaderSize, 10000);
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

                        mError              = false;
                        mUSBCheckSuccessful = false;

                        mReceiveStitching = false;
                        mSendStitching    = false;

                        mAsyncSendBufferMutex.lock();
                        std::queue<std::array<char, cMaxAsynchronousBuffer>>().swap(mAsyncSendBuffer);
                        mAsyncSendBufferMutex.unlock();

                        std::array<char, cMaxAsynchronousBuffer>().swap(mPacketToSend);
                        mBytesSent          = 0;
                        mBytesInAsyncBuffer = 0;

                        Logger::GetInstance().Log("Ran into a snag, restarting stack!", Logger::Level::WARNING);
                        std::this_thread::sleep_for(100ms);
                    } else if (mRetryCounter >= cMaxRetries) {
                        Logger::GetInstance().Log("Too many errors! Bailing out!", Logger::Level::ERROR);
                        CloseDevice();
                        mRetryCounter = 0;
                    }

                    if (!mSendStitching) {
                        // First read, then write
                        int lLength{USBBulkRead(0x81, 512, 1000)};
                        if (lLength > 0) {
                            mLength = lLength;
                            ReceiveCallback();
                        } else if (lLength == LIBUSB_ERROR_TIMEOUT) {
                            // Timeout errors are probably recoverable
                            std::this_thread::sleep_for(100ms);
                        } else if (lLength == LIBUSB_ERROR_BUSY) {
                            // Also not fatal probably, wait another 10ms
                            std::this_thread::sleep_for(10ms);
                        } else {
                            // Probably fatal, try a restart of the device
                            mError = true;
                        }
                    }

                    if (!mReceiveStitching) {
                        if (mError) {
                            // Clear the send buffer when an error occured because the buffer will fill up quickly and
                            // the data is probably no longer relevant
                            mAsyncSendBufferMutex.lock();
                            std::queue<std::array<char, cMaxAsynchronousBuffer>>().swap(mAsyncSendBuffer);
                            mAsyncSendBufferMutex.unlock();
                        }
                        if (!mAsyncSendBuffer.empty()) {
                            // Pop the first packet from the buffer if our previous packet was processed
                            if (mPacketToSend.empty()) {
                                Logger::GetInstance().Log("Popping packet from buffer", Logger::Level::TRACE);
                                mAsyncSendBufferMutex.lock();
                                mAsyncSendBuffer.front().swap(mPacketToSend);
                                mAsyncSendBuffer.pop();
                                mAsyncSendBufferMutex.unlock();
                            }

                            if (!mPacketToSend.empty()) {
                                size_t      lPacketSize{0};
                                std::string lPacket{};

                                // First add the packet header
                                AsyncCommand lCommand{};
                                memset(&lCommand, 0, sizeof(lCommand));
                                lCommand.channel = cAsyncUserChannel;
                                lCommand.magic   = Asynchronous;

                                lPacket.append(reinterpret_cast<char*>(&lCommand), cAsyncHeaderSize);
                                lPacketSize += cAsyncHeaderSize;

                                // If we are not stitching we need to add a subheader
                                if (!mSendStitching) {
                                    mBytesSent = 0;

                                    AsyncSubHeader lSubHeader{};
                                    memset(&lSubHeader, 0, sizeof(lSubHeader));
                                    lSubHeader.magic = cAsyncCommandSendPacket;
                                    lSubHeader.mode  = cAsyncModeDebug;
                                    lSubHeader.ref   = cAsyncCommandSendPacket;
                                    lSubHeader.size  = mPacketToSend.size();

                                    lPacket.append(reinterpret_cast<char*>(&lSubHeader), cAsyncSubHeaderSize);

                                    lPacketSize += cAsyncSubHeaderSize;
                                }

                                // Packet is too big, start stitching
                                if (mPacketToSend.size() - mBytesSent + lPacketSize > cMaxUSBBuffer) {
                                    Logger::GetInstance().Log("SendStitching", Logger::Level::TRACE);
                                    mSendStitching = true;
                                    // Start from bytes sent, then add the max usb buffer - header size
                                    lPacket.append(mPacketToSend.at(mBytesSent), mPacketToSend.at(mBytesSent + cMaxUSBBuffer - lPacketSize - 1));
                                    // We don't count the header size for bytes sent
                                    mBytesSent += cMaxUSBBuffer - lPacketSize; 
                                    lPacketSize = cMaxUSBBuffer;
                                } else {
                                    mSendStitching = false;
                                    lPacketSize    += mPacketToSend.size() - mBytesSent;
                                    lPacket.append(mPacketToSend.at(mBytesSent), mPacketToSend.at(mBytesSent + lPacketSize - 1));
								    Logger::GetInstance().Log("Endstitch", Logger::Level::TRACE);

                                    std::array<char, cMaxAsynchronousBuffer>().swap(mPacketToSend);
                                }
                                if (USBBulkWrite(0x3, lPacket, lPacketSize, 1000) == -1) {
                                    mError = true;
                                }
                            }
                        }
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

void USBReader::Send(std::string_view aData)
{
    const std::lock_guard<std::mutex> lLock(mAsyncSendBufferMutex);
    // We limit the send buffer size to 100, that is 230400 bytes of ram used.
    // If this increases latency too much, lower this.
    if (mAsyncSendBuffer.size() < cMaxSendBufferItems) {
        std::array<char, cMaxAsynchronousBuffer> lArray{};
        aData.copy(lArray.data(), aData.size());
        mAsyncSendBuffer.push(lArray);
    }
    if (mAsyncSendBuffer.size() > 10) {
        Logger::GetInstance().Log(
            "Sendbuffer got above 20 packets, some latency expected: " + std::to_string(mAsyncSendBuffer.size()),
            Logger::Level::WARNING);
    }
}

USBReader::~USBReader()
{
    if(mDeviceHandle != nullptr)
    {
        CloseDevice();
    }
}

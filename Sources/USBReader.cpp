#include "../Includes/USBReader.h"

/* Copyright (c) 2021 [Rick de Bondt] - USBReader.cpp */

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Not needed for this file but xlinkconnection needs it and this solves an ordering issue on windows
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <sys/types.h>

// This garbage is needed for it to compile when building statically
#ifdef BUILD_STATIC
#ifdef _MSC_VER
#pragma comment(lib, "legacy_stdio_definitions.lib")
#ifdef __cplusplus
FILE iob[] = {*stdin, *stdout, *stderr};
extern "C" {
    FILE* __cdecl _iob(void) { return iob; }
}
#endif
#endif
#endif

#include <libusb.h>

#include "../Includes/Logger.h"
#include "../Includes/NetConversionFunctions.h"
#include "../Includes/USBReceiveThread.h"
#include "../Includes/USBSendThread.h"
#include "../Includes/XLinkKaiConnection.h"

#if defined(_MSC_VER) && defined(__MINGW32__)
#include <hidclass.h>
#endif

namespace
{
    constexpr unsigned int cPSPVID{0x54C};
    constexpr unsigned int cPSPPID{0x1C9};
}  // namespace

using namespace std::chrono_literals;
using namespace USB_Constants;

USBReader::USBReader(
    int aMaxBufferedMessages, int aMaxFatalRetries, int aMaxReadWriteRetries, int aReadTimeoutMS, int aWriteTimeoutMS) :
    mMaxBufferedMessages(aMaxBufferedMessages),
    mMaxFatalRetries(aMaxFatalRetries), mMaxReadWriteRetries(aMaxReadWriteRetries), mReadTimeoutMS(aReadTimeoutMS),
    mWriteTimeoutMS(aWriteTimeoutMS)
{
    libusb_init(nullptr);
}

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
                Logger::GetInstance().Log("Size reported: " + std::to_string(lSubHeader->size), Logger::Level::TRACE);
                lReturn = cAsyncModePacket;
            } else if (lSubHeader->mode == cAsyncModeDebug) {
                lReturn = cAsyncModeDebug;
            }
        }
    }
    return lReturn;
}

void USBReader::Close()
{
    // Close thread nicely
    mStopRequest = true;
    
    if (mUSBThread != nullptr) {
        while (mStopRequest && !mUSBThread->joinable()) {
            std::this_thread::sleep_for(1s);
        }
        mUSBThread->join();
        mUSBThread = nullptr;
    } else {
        HandleClose();
    }

    if (mUSBSendThread != nullptr) {
        mUSBSendThread->StopThread();
        mUSBSendThread = nullptr;
    }
    if (mUSBReceiveThread != nullptr) {
        mUSBReceiveThread->StopThread();
        mUSBReceiveThread = nullptr;
    }
}

void USBReader::HandleAsynchronous(AsyncCommand& aData, int aLength)
{
    if (aData.channel == cAsyncUserChannel) {
        BinaryStitchUSBPacket lPacket{};
        if (!mReceiveStitching) {
            int lPacketMode{IsDebugPrintCommand(aData, aLength)};

            if (lPacketMode > 0) {
                // We know it's a DebugPrint command, so we can skip past this header as well now
                unsigned int lLength = aLength - cAsyncHeaderAndSubHeaderSize;
                int          lActualPacketLength{0};
                // We are a packet, so we can check if we can send it off
                switch (lPacketMode) {
                    case cAsyncModePacket:
                        // Grab the packet length from the packet
                        lActualPacketLength =
                            reinterpret_cast<AsyncSubHeader*>(reinterpret_cast<char*>(&aData) + cAsyncHeaderSize)->size;

                        lPacket.stitch    = lActualPacketLength > (cMaxUSBPacketSize - cAsyncHeaderAndSubHeaderSize);
                        mReceiveStitching = lPacket.stitch;

                        // Skip headers already
                        lPacket.length = aLength - cAsyncHeaderAndSubHeaderSize;
                        memcpy(lPacket.data.data(),
                               reinterpret_cast<char*>(&aData) + cAsyncHeaderAndSubHeaderSize,
                               lPacket.length);

                        mUSBReceiveThread->AddToQueue(lPacket);
                        break;
                    case cAsyncModeDebug:
                        // We can just go ahead and print the debug data, I'm assuming it will never go past 512 bytes.
                        // If it does, we'll see when we get there :|
                        Logger::GetInstance().Log(
                            "PSP: " +
                                std::string(reinterpret_cast<char*>(&aData) + cAsyncHeaderAndSubHeaderSize, lLength),
                            Logger::Level::INFO);
                        break;
                    default:
                        // Don't know what we got
                        Logger::GetInstance().Log(
                            "Unknown data:" + PrettyHexString(std::string(reinterpret_cast<char*>(&aData), mLength)),
                            Logger::Level::DEBUG);
                }
            } else {
                // Don't know what we got
                Logger::GetInstance().Log(
                    "Unknown data:" + PrettyHexString(std::string(reinterpret_cast<char*>(&aData), mLength)),
                    Logger::Level::DEBUG);
            }
        } else {
            lPacket.stitch    = aLength > (cMaxUSBPacketSize - cAsyncHeaderSize);
            mReceiveStitching = lPacket.stitch;

            // Skip headers already
            lPacket.length = aLength - cAsyncHeaderSize;
            memcpy(lPacket.data.data(), reinterpret_cast<char*>(&aData) + cAsyncHeaderSize, lPacket.length);

            mUSBReceiveThread->AddToQueue(lPacket);
        }
    }
}

void USBReader::HandleAsynchronousSend()
{
    while ((!mStopRequest) && mUSBSendThread->HasOutgoingData()) {
        BinaryStitchUSBPacket lFormattedPacket = mUSBSendThread->PopFromOutgoingQueue();
        mSendStitching                         = lFormattedPacket.stitch;
        if (USBBulkWrite(
                cUSBDataWriteEndpoint, lFormattedPacket.data.data(), lFormattedPacket.length, mWriteTimeoutMS) == -1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(mWriteTimeoutMS));
            mReadWriteRetryCounter++;
            if (mReadWriteRetryCounter > mMaxReadWriteRetries) {
                mError = true;
            }
        }
    }
}

void USBReader::HandleClose()
{
    if (mDeviceHandle != nullptr) {
        libusb_reset_device(mDeviceHandle);
        libusb_release_interface(mDeviceHandle, 0);
        libusb_attach_kernel_driver(mDeviceHandle, 0);
        libusb_close(mDeviceHandle);
        mDeviceHandle = nullptr;
    }
    mStopRequest = false;
}

void USBReader::HandleError()
{
    // Do a full reset
    HandleClose();
    Open();
    mRetryCounter++;

    mError              = false;
    mUSBCheckSuccessful = false;

    mReceiveStitching = false;
    mSendStitching    = false;

    mUSBSendThread->ClearQueues();
    mUSBReceiveThread->ClearQueues();

    Logger::GetInstance().Log("Ran into a snag, restarting stack!", Logger::Level::DEBUG);
    std::this_thread::sleep_for(1ms);
}

bool USBReader::Open()
{
    libusb_device_handle* lDeviceHandle{nullptr};
    libusb_device**       lDevices{nullptr};
    libusb_device*        lDevice{nullptr};
    int                   lAmountOfDevices{0};
    int                   lReturn{0};

    lAmountOfDevices = libusb_get_device_list(nullptr, &lDevices);
    if (lAmountOfDevices >= 0 && lDevices != nullptr) {
        for(int lCount = 0; (lCount < lAmountOfDevices) && (mDeviceHandle == nullptr); lCount++) {
            lDevice = lDevices[lCount];
            libusb_device_descriptor lDescriptor{0};
            memset(&lDescriptor, 0, sizeof(lDescriptor));
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
                        Logger::GetInstance().Log(std::string("Could not open USB device: ") +
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
                Logger::GetInstance().Log(std::string("Cannot query device descriptor: ") +
                                              libusb_strerror(static_cast<libusb_error>(lReturn)),
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
    int lError{0};
    if (mDeviceHandle != nullptr) {
        lError = libusb_bulk_transfer(mDeviceHandle,
                                      aEndpoint,
                                      reinterpret_cast<unsigned char*>(mTemporaryReceiveBuffer.data()),
                                      aSize,
                                      &lReturn,
                                      aTimeOut);
        if (lError != 0) {
            lReturn = lError;
        }
    } else {
        lReturn = -1;
    }

    return lReturn;
}

int USBReader::USBBulkWrite(int aEndpoint, char* aData, int aSize, int aTimeOut)
{
    int lReturn{-1};

    if (mDeviceHandle != nullptr) {
        int lError = libusb_bulk_transfer(
            mDeviceHandle, aEndpoint, reinterpret_cast<unsigned char*>(aData), aSize, &lReturn, aTimeOut);
        if (lError < 0) {
            Logger::GetInstance().Log(
                std::string("Error during Bulk write: ") + libusb_strerror(static_cast<libusb_error>(lError)),
                Logger::Level::ERROR);
            lReturn = -1;
        }
    } else {
        lReturn = -1;
    }

    return lReturn;
}

bool USBReader::USBCheckDevice()
{
    bool lReturn{true};
    Logger::GetInstance().Log("USBCheckDevice", Logger::Level::TRACE);

    if (mDeviceHandle != nullptr) {
        int lMagic = HostFS;
        int lLength =
            USBBulkWrite(cUSBHelloEndpoint, reinterpret_cast<char*>(&lMagic), sizeof(int), cMaxUSBHelloTimeout);
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

    return USBBulkWrite(cUSBHelloEndpoint, reinterpret_cast<char*>(&lResponse), cHostFSHeaderSize, cMaxUSBHelloTimeout);
}

bool USBReader::StartReceiverThread()
{
    bool lReturn{true};

    if (mDeviceHandle != nullptr && mUSBThread == nullptr) {
        mUSBReceiveThread = std::make_shared<USBReceiveThread>(*mIncomingConnection, mMaxBufferedMessages);
        mUSBReceiveThread->StartThread();

        mUSBSendThread = std::make_shared<USBSendThread>(mMaxBufferedMessages);
        mUSBSendThread->StartThread();

        mUSBThread = std::make_shared<boost::thread>([&] {
            // If we didn't get a graceful disconnect retry making connection.
            while ((mDeviceHandle != nullptr) || mRetryCounter > 0) {
                if (mStopRequest) {
                    HandleClose();
                } else {
                    if (!mUSBCheckSuccessful) {
                        mUSBCheckSuccessful = USBCheckDevice();
                    }

                    if ((mError && mRetryCounter < mMaxFatalRetries) || (!mUSBCheckSuccessful)) {
                        HandleError();
                    } else if (mRetryCounter >= mMaxFatalRetries) {
                        Logger::GetInstance().Log("Too many errors! Bailing out!", Logger::Level::ERROR);
                        HandleClose();
                        mRetryCounter = 0;
                    }

                    if (!mSendStitching) {
                        // First read, then write
                        int lLength{USBBulkRead(cUSBDataReadEndpoint, cMaxUSBPacketSize, mReadTimeoutMS)};
                        if (lLength > 0) {
                            mLength = lLength;
                            mRetryCounter = 0;
                            ReceiveCallback();
                        } else if (lLength == LIBUSB_ERROR_TIMEOUT || lLength == LIBUSB_ERROR_BUSY) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(mReadTimeoutMS));
                        } else if (mDeviceHandle == nullptr) {
                            mError = true;
                        } else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(mReadTimeoutMS));
                            mReadWriteRetryCounter++;
                            if (mReadWriteRetryCounter > mMaxReadWriteRetries) {
                                mError = true;
                            }
                            // Probably fatal, try a restart of the device
                        }
                    }

                    if (!mReceiveStitching) {
                        HandleAsynchronousSend();
                    }
                }
            }
        });
    } else {
        lReturn = false;
    }
    return lReturn;
}

void USBReader::Send(std::string_view aData)
{
    // Handle in send thread
    mUSBSendThread->AddToQueue(aData);
}

USBReader::~USBReader()
{
    Close();
    libusb_exit(nullptr);
}

#include "../Includes/USBSendThread.h"

/* Copyright (c) 2021 [Rick de Bondt] - USBSendThread.cpp */

#include <boost/thread.hpp>

#include "../Includes/Logger.h"
#include "../Includes/NetConversionFunctions.h"
#include "../Includes/XLinkKaiConnection.h"

USBSendThread::USBSendThread(int aMaxBufferSize) : mMaxBufferSize(aMaxBufferSize) {}

bool USBSendThread::StartThread()
{
    bool lReturn{true};
    if (mThread == nullptr) {
        mDone   = false;
        lReturn = true;
        mThread = std::make_shared<boost::thread>([&] {
            while (!mStopRequest) {
                mMutex.lock();
                if (!mQueue.empty()) {
                    // Do a deep copy so we can keep this mutex locked as short as possible
                    USB_Constants::BinaryWiFiPacket lFrontOfQueue{mQueue.front()};
                    mQueue.pop();
                    mMutex.unlock();

                    int lPacketIndex{0};
                    mLastReceivedPacket = lFrontOfQueue;
                    bool lContinue{true};
                    while (lPacketIndex < lFrontOfQueue.length) {
                        int lPacketSize{0};
                        int lHeaderLength{0};

                        USB_Constants::BinaryStitchUSBPacket lPacket{};

                        // First packet when stitching has a bigger header size
                        if (lPacketIndex == 0) {
                            lHeaderLength = USB_Constants::cAsyncHeaderAndSubHeaderSize;
                        } else {
                            lHeaderLength = USB_Constants::cAsyncHeaderSize;
                        }

                        // If the size is bigger than the buffer, start stitching
                        lPacket.stitch =
                            (lFrontOfQueue.length - lPacketIndex) > (USB_Constants::cMaxUSBPacketSize - lHeaderLength);

                        // Length is either the size of the USB buffer or what's left of the packet
                        unsigned int lLength{lPacket.stitch ? USB_Constants::cMaxUSBPacketSize - lHeaderLength :
                                                              lFrontOfQueue.length - lPacketIndex};

                        // First add the packet header
                        USB_Constants::AsyncCommand lCommand{};
                        memset(&lCommand, 0, sizeof(lCommand));
                        lCommand.channel = USB_Constants::cAsyncUserChannel;
                        lCommand.magic   = USB_Constants::Asynchronous;

                        memcpy(lPacket.data.data(), &lCommand, USB_Constants::cAsyncHeaderSize);
                        lPacketSize += USB_Constants::cAsyncHeaderSize;

                        // First packet needs a subheader
                        if (lPacketIndex == 0) {
                            USB_Constants::AsyncSubHeader lSubHeader{};
                            memset(&lSubHeader, 0, USB_Constants::cAsyncSubHeaderSize);
                            lSubHeader.magic = USB_Constants::DebugPrint;
                            lSubHeader.mode  = 3;
                            lSubHeader.ref   = 0;  // i don't know why this is 0
                            lSubHeader.size  = lFrontOfQueue.length;
                            Logger::GetInstance().Log("size = " + std::to_string(lFrontOfQueue.length),
                                                      Logger::Level::TRACE);

                            memcpy(lPacket.data.data() + lPacketSize, &lSubHeader, USB_Constants::cAsyncSubHeaderSize);
                            lPacketSize += USB_Constants::cAsyncSubHeaderSize;
                        }

                        memcpy(lPacket.data.data() + lPacketSize, lFrontOfQueue.data.data() + lPacketIndex, lLength);
                        lPacketSize += lLength;

                        lPacket.length = lPacketSize;
                        lPacketIndex += lLength;
                        mOutgoingQueue.push(lPacket);
                    }
                } else {
                    // Never forget to unlock a mutex
                    mMutex.unlock();
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        });
        mDone   = true;
    }
    return lReturn;
}

void USBSendThread::StopThread()
{
    mStopRequest = true;
    while (!mDone) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    ClearQueues();
    if (mThread != nullptr && mThread->joinable()) {
        mThread->join();
    }
    mThread = nullptr;
}

bool USBSendThread::AddToQueue(std::string_view aData)
{
    bool lReturn{false};
    if (mQueue.size() < mMaxBufferSize) {
        lReturn = true;
        USB_Constants::BinaryWiFiPacket lPacket{};
        memcpy(lPacket.data.data(), aData.data(), aData.size());
        lPacket.length = aData.size();

        mMutex.lock();
        mQueue.push(lPacket);
        mMutex.unlock();
        if (mQueue.size() > 50) {
            Logger::GetInstance().Log("Sendbuffer got to over 50! " + std::to_string(mQueue.size()),
                                      Logger::Level::WARNING);
        }
    } else {
        Logger::GetInstance().Log("Sendbuffer filled up!", Logger::Level::ERROR);
    }
    return lReturn;
}

bool USBSendThread::HasOutgoingData()
{
    return !mOutgoingQueue.empty();
}

USB_Constants::BinaryStitchUSBPacket USBSendThread::PopFromOutgoingQueue()
{
    USB_Constants::BinaryStitchUSBPacket lReturn{};
    if (!mOutgoingQueue.empty()) {
        mMutex.lock();
        lReturn = mOutgoingQueue.front();
        mOutgoingQueue.pop();
        mMutex.unlock();
    }
    return lReturn;
}

void USBSendThread::ClearQueues()
{
    mMutex.lock();
    std::queue<USB_Constants::BinaryWiFiPacket>().swap(mQueue);
    std::queue<USB_Constants::BinaryStitchUSBPacket>().swap(mOutgoingQueue);
    mLastReceivedPacket = {};
    mMutex.unlock();
}
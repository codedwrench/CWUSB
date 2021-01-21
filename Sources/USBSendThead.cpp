#include "../Includes/USBSendThread.h"

/* Copyright (c) 2021 [Rick de Bondt] - USBSendThread.cpp */

#include "../Includes/Logger.h"
#include "../Includes/XLinkKaiConnection.h"

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
                    size_t                          lQueueSize{mQueue.size()};
                    mQueue.pop();
                    mMutex.unlock();

                    // If there is a message available and it is unique, add it
                    if ((lFrontOfQueue.length != mLastReceivedPacket.length) &&
                        (lFrontOfQueue.data != mLastReceivedPacket.data)) {
                        mLastReceivedPacket = lFrontOfQueue;
                        int lPacketSize{0};

                        USB_Constants::BinaryStitchUSBPacket lPacket{};
                        unsigned int lHeaderLength = mLastPacketStitched ? USB_Constants::cAsyncHeaderSize :
                                                                           USB_Constants::cAsyncHeaderAndSubHeaderSize;
                        lPacket.stitch             = lFrontOfQueue.length > (cMaxUSBBuffer - lHeaderLength);
                        int lLength{lPacket.stitch ? cMaxUSBBuffer : lFrontOfQueue.length};

                        // First add the packet header
                        USB_Constants::AsyncCommand lCommand{};
                        memset(&lCommand, 0, sizeof(lCommand));
                        lCommand.channel = USB_Constants::cAsyncUserChannel;
                        lCommand.magic   = USB_Constants::Asynchronous;

                        memcpy(lPacket.data.data(), &lCommand, USB_Constants::cAsyncHeaderSize);
                        lPacketSize += USB_Constants::cAsyncHeaderSize;

                        // If we are not stitching we need to add a subheader
                        if (!mLastPacketStitched) {
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

                        memcpy(lPacket.data.data() + lPacketSize, lFrontOfQueue.data.data(), lLength);
                        lPacket.length = lLength;
                        mOutgoingQueue.push(lPacket);
                        mLastPacketStitched = lPacket.stitch;
                    }
                } else {
                    // Never forget to unlock a mutex
                    mMutex.unlock();
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            mDone = true;
        });
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
    if (mThread->joinable()) {
        mThread->join();
    }
    mThread = nullptr;
}

bool USBSendThread::AddToQueue(std::string_view aData)
{
    bool lReturn{false};
    if (mQueue.size() < USBSendThread_Constants::cMaxQueueSize) {
        lReturn = true;
        USB_Constants::BinaryWiFiPacket lPacket{};
        memcpy(lPacket.data.data(), aData.data(), aData.size());
        lPacket.length = aData.size();

        mMutex.lock();
        mQueue.push(lPacket);
        mMutex.unlock();
        if (mQueue.size() > (USBSendThread_Constants::cMaxQueueSize / 2)) {
            Logger::GetInstance().Log("Sendbuffer got to over half its capacity! " + std::to_string(mQueue.size()),
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
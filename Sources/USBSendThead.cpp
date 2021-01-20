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

                        // The length is too high, this needs to be split
                        USB_Constants::BinaryStitchUSBPacket lPacket{};
                        lPacket.stitch = lFrontOfQueue.length > cMaxUSBBuffer ? cMaxUSBBuffer : lFrontOfQueue.length;
                        int lLength{lPacket.stitch ? cMaxUSBBuffer : lFrontOfQueue.length};

                        memcpy(lPacket.data.data(), lFrontOfQueue.data.data(), lLength);
                        lPacket.length = lLength;
                        mOutgoingQueue.push(lPacket);
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
        Logger::GetInstance().Log("Receivebuffer filled up!", Logger::Level::ERROR);
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
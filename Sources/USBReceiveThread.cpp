#include "../Includes/USBReceiveThread.h"

/* Copyright (c) 2021 [Rick de Bondt] - USBReceiveThread.cpp */

#include <boost/thread.hpp>

#include "../Includes/Logger.h"
#include "../Includes/XLinkKaiConnection.h"

USBReceiveThread::USBReceiveThread(XLinkKaiConnection& aConnection, int aMaxBufferSize) :
    mConnection(aConnection), mMaxBufferSize(aMaxBufferSize)
{}

bool USBReceiveThread::StartThread()
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
                    USB_Constants::BinaryStitchUSBPacket lFrontOfQueue(mQueue.front());
                    size_t                               lQueueSize{mQueue.size()};
                    mQueue.pop();
                    mMutex.unlock();


                    // If the last message was too big for the USB-buffer, append the current one.
                    if (mLastReceivedMessage.stitch) {
                        memcpy(mLastReceivedMessage.data.data() + mLastReceivedMessage.length,
                               lFrontOfQueue.data.data(),
                               lFrontOfQueue.length);

                        mLastReceivedMessage.length += lFrontOfQueue.length;
                        mLastReceivedMessage.stitch = lFrontOfQueue.stitch;
                    } else {
                        // Replace last received since the previous one wasn't a stitch packet
                        memcpy(mLastReceivedMessage.data.data(), lFrontOfQueue.data.data(), lFrontOfQueue.length);

                        mLastReceivedMessage.length = lFrontOfQueue.length;
                        mLastReceivedMessage.stitch = lFrontOfQueue.stitch;
                    }

                    // Check if we should continue stitching
                    if (mLastReceivedMessage.length > USB_Constants::cMaxAsynchronousBuffer) {
                        Logger::GetInstance().Log("Something went wrong while stitching. Dropping packet!",
                                                  Logger::Level::ERROR);
                        mLastReceivedMessage = {};
                    // Check if we can send this off
                    } else if (!lFrontOfQueue.stitch) {
                        mConnection.Send(
                            std::string_view(mLastReceivedMessage.data.data(), mLastReceivedMessage.length));
                        mLastReceivedMessage = {};
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

void USBReceiveThread::StopThread()
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

bool USBReceiveThread::AddToQueue(const USB_Constants::BinaryStitchUSBPacket& aStruct)
{
    bool lReturn{false};
    if (mQueue.size() < mMaxBufferSize) {
        lReturn = true;
        mMutex.lock();
        mQueue.push(aStruct);
        mMutex.unlock();
        if (mQueue.size() > 50) {
            Logger::GetInstance().Log("Receivebuffer got to over 50! " + std::to_string(mQueue.size()),
                                      Logger::Level::WARNING);
        }
    } else {
        Logger::GetInstance().Log("Receivebuffer filled up!", Logger::Level::ERROR);
    }
    return lReturn;
}

void USBReceiveThread::ClearQueues()
{
    mMutex.lock();
    std::queue<USB_Constants::BinaryStitchUSBPacket>().swap(mQueue);
    mLastReceivedMessage = {};
    mMutex.unlock();
}


USBReceiveThread::~USBReceiveThread()
{
    StopThread();
}

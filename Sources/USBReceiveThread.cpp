#include "../Includes/USBReceiveThread.h"

/* Copyright (c) 2021 [Rick de Bondt] - USBReceiveThread.cpp */

#include <boost/thread.hpp>

#include "../Includes/Logger.h"
#include "../Includes/XLinkKaiConnection.h"

USBReceiveThread::USBReceiveThread(XLinkKaiConnection& aConnection) : mConnection(aConnection) {}

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

                    // If there is a message available and it is unique, add it
                    if ((lFrontOfQueue.length != mLastReceivedMessage.length) &&
                        (lFrontOfQueue.data.data() != mLastReceivedMessage.data.data())) {
                        bool lUseLastReceived{false};
                        // If the last message was too big for the USB-buffer, append the current one.
                        if (mLastReceivedMessage.stitch) {
                            memcpy(mLastReceivedMessage.data.data() + mLastReceivedMessage.length,
                                   lFrontOfQueue.data.data(),
                                   lFrontOfQueue.length);

                            mLastReceivedMessage.length += lFrontOfQueue.length;
                            lUseLastReceived = true;
                        } else {
                            // Normally this does not happen
                            Logger::GetInstance().Log("Something went wrong when stitching", Logger::Level::WARNING);
                            mLastCompleteMessage = mLastReceivedMessage;
                            mLastReceivedMessage = {};
                        }

                        if (!lFrontOfQueue.stitch) {
                            // If we just finished our last message, use that, if this is a totally new message, use the
                            // front of queue.
                            if (lUseLastReceived) {
                                if (mLastReceivedMessage.length != mLastCompleteMessage.length &&
                                    mLastReceivedMessage.data != mLastCompleteMessage.data) {
                                    mLastCompleteMessage = mLastReceivedMessage;
                                    mConnection.Send(std::string_view(mLastReceivedMessage.data.data(),
                                                                      mLastReceivedMessage.length));
                                }
                            } else if (lFrontOfQueue.length != mLastCompleteMessage.length &&
                                       memcmp(lFrontOfQueue.data.data(),
                                              mLastCompleteMessage.data.data(),
                                              lFrontOfQueue.length) != 0) {
                                memcpy(
                                    mLastCompleteMessage.data.data(), lFrontOfQueue.data.data(), lFrontOfQueue.length);
                                mLastCompleteMessage.length = lFrontOfQueue.length;
                                mConnection.Send(std::string_view(lFrontOfQueue.data.data(), lFrontOfQueue.length));
                            }
                        }
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
    if (mThread->joinable()) {
        mThread->join();
    }
    mThread = nullptr;
}

bool USBReceiveThread::AddToQueue(const USB_Constants::BinaryStitchUSBPacket& aStruct)
{
    bool lReturn{false};
    if (mQueue.size() < USBReceiveThread_Constants::cMaxQueueSize) {
        lReturn = true;
        mMutex.lock();
        mQueue.push(aStruct);
        mMutex.unlock();
        if (mQueue.size() > (USBReceiveThread_Constants::cMaxQueueSize / 2)) {
            Logger::GetInstance().Log("Receivebuffer got to over half its capacity! " + std::to_string(mQueue.size()),
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
    mLastCompleteMessage = {};
    mLastReceivedMessage = {};
    mMutex.unlock();
}


USBReceiveThread::~USBReceiveThread()
{
    StopThread();
}
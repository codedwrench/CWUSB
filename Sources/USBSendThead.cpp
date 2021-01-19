#include "../Includes/Logger.h"
#include "../Includes/USBSendThread.h"
#include "../Includes/XLinkKaiConnection.h"

USBSendThread::USBSendThread(XLinkKaiConnection& aConnection) : mConnection(aConnection) {}

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
    if (mThread->joinable()) {
        mThread->join();
    }
    mThread = nullptr;
}

bool USBSendThread::AddToQueue(std::string_view aData)
{
    if (mQueue.size() < USBSendThread_Constants::cMaxQueueSize) {
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
}
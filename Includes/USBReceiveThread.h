#pragma once
/* Copyright (c) 2021 [Rick de Bondt] - USBReceiveThread.h
 *
 * This file contains the header for a USBReceiveThread class which will be used to read data from the PSP.
 *
 **/

#include <memory>
#include <mutex>

#include <boost/thread.hpp>

#include "USBConstants.h"

namespace USBReceiveThread_Constants
{
    static constexpr int cMaxQueueSize{20};
};

class XLinkKaiConnection;

class USBReceiveThread
{
public:
    /**
     * Constructor for USBReceiveThread.
     * @param aConnection - The connection to send the data on.
     */
    explicit USBReceiveThread(XLinkKaiConnection& aConnection);
    ~USBReceiveThread();
    USBReceiveThread(const USBReceiveThread& aUSBReceiveThread) = delete;
    USBReceiveThread& operator=(const USBReceiveThread& aUSBReceiveThread) = delete;

    /**
     * Starts the thread to receive data from USB.
     * @return true if successful.
     */
    bool StartThread();

    /**
     * Stops the thread.
     */
    void StopThread();

    /**
     * Adds a message to the queue.
     * @param aStruct - structure containing the message, length and if it should be stitched.
     * @return true if queue not full.
     */
    bool AddToQueue(const USB_Constants::BinaryStitchUSBPacket& aStruct);

    /**
     * Clears all the queues in this class.
     */
    void ClearQueues();

private:
    XLinkKaiConnection&                              mConnection;
    bool                                             mDone{true};
    bool                                             mError{false};
    USB_Constants::BinaryStitchWiFiPacket            mLastReceivedMessage{};
    USB_Constants::BinaryStitchWiFiPacket            mLastCompleteMessage{};
    std::mutex                                       mMutex{};
    std::queue<USB_Constants::BinaryStitchUSBPacket> mQueue{};
    bool                                             mStopRequest{false};
    std::shared_ptr<boost::thread>                   mThread{nullptr};
};
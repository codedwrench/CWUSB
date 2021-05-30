#pragma once
/* Copyright (c) 2020 [Rick de Bondt] - USBSendThread.h
 *
 * This file contains the header for a USBSendThread class which will be used to grab data from XLink and format it
 * for the PSP.
 **/

#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "USBConstants.h"

class XLinkKaiConnection;

class USBSendThread
{
public:
    explicit USBSendThread(int aMaxBufferSize);

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
     * @param aData - the data to put into the buffer.
     * @return true if queue not full.
     */
    bool AddToQueue(std::string_view aData);

    /**
     * Clears the queues in this object.
     */
    void ClearQueues();

    /**
     * Checks if there is data in the queue.
     * @return true if there is data.
     */
    bool HasOutgoingData();

    /**
     * Grabs a packet from the outgoing queue if the buffer is not empty.
     */
    USB_Constants::BinaryStitchUSBPacket PopFromOutgoingQueue();

private:
    int                                              mMaxBufferSize{0};
    bool                                             mDone{true};
    bool                                             mError{false};
    std::mutex                                       mMutex{};
    USB_Constants::BinaryWiFiPacket                  mLastReceivedPacket{};
    std::queue<USB_Constants::BinaryWiFiPacket>      mQueue{};
    std::queue<USB_Constants::BinaryStitchUSBPacket> mOutgoingQueue{};
    bool                                             mStopRequest{false};
    std::shared_ptr<std::thread>                     mThread{nullptr};
};

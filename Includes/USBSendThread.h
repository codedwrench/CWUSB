#pragma once
#include "USBConstants.h"

/* Copyright (c) 2020 [Rick de Bondt] - USBSendThread.h
 *
 * This file contains the header for a USBSendThread class which will be used to grab data from XLink and format it
 * for the PSP.
 **/

namespace USBSendThread_Constants
{
    static constexpr int cMaxQueueSize{20};
};

class XLinkKaiConnection;

class USBSendThread
{
public:
    /**
     * Constructor for USBSendThread.
     * @param aConnection - The connection to send the data on.
     */
    explicit USBSendThread(XLinkKaiConnection& aConnection);
    ~USBSendThread();
    USBSendThread(const USBSendThread& aUSBSendThread) = delete;
    USBSendThread& operator=(const USBSendThread& aUSBSendThread) = delete;

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

private:
    XLinkKaiConnection&                              mConnection;
    bool                                             mDone{true};
    bool                                             mError{false};
    std::mutex                                       mMutex{};
    USB_Constants::BinaryWiFiPacket                  mLastReceivedPacket{};
    std::queue<USB_Constants::BinaryWiFiPacket>      mQueue{};
    std::queue<USB_Constants::BinaryStitchUSBPacket> mOutgoingQueue{};
    bool                                             mStopRequest{false};
    std::shared_ptr<boost::thread>                   mThread{nullptr};
};
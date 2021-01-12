#pragma once

/* Copyright (c) 2020 [Rick de Bondt] - USBReader.h
 *
 * This file contains the header for a USBReader class which will be used to read/write data from/to the PSP.
 *
 * */

#include <array>
#include <iostream>
#include <mutex>
#include <queue>
#include <string_view>

#include <boost/thread.hpp>

#include <libusb.h>

#include "../Includes/USBConstants.h"

constexpr int cMaxUSBBuffer = 512;

// The maximum 802.11 MTU is 2304 bytes. 802.11-2012, page 413, section 8.3.2.1
constexpr int cMaxAsynchronousBuffer = 2304;

class XLinkKaiConnection;

class USBReader
{
public:
    /**
     * Closes the device
     */
    void CloseDevice();

    /**
     * Opens the the USB device, so we can send/read data.
     * @return true if successful.
     */
    bool OpenDevice();

    /**
     * Sends a Bulk In request on the USB bus.
     * @param aEndpoint - The endpoint to use.
     * @param aSize - Size of data to send.
     * @param aTimeout - The timeout of the request to set.
     * @return Amount of bytes read, < 0 on error .
     */
    int USBBulkRead(int aEndpoint, int aSize, int aTimeOut);

    /**
     * Sends a Bulk Out request on the USB bus.
     * @param aEndpoint - The endpoint to use.
     * @param aData - Data to send in request
     * @param aSize - Size of data to send.
     * @param aTimeout - The timeout of the request to set.
     * @return 0 on success, < 0 on error .
     */
    int USBBulkWrite(int aEndpoint, std::string_view aData, int aSize, int aTimeOut);

    /**
     * Handles traffic from USB.
     */
    void ReceiveCallback();

    void SetIncomingConnection(std::shared_ptr<XLinkKaiConnection> aDevice);

    void Send(std::string_view aData);

    bool StartReceiverThread();

private:
    bool USBCheckDevice();
    void HandleAsynchronous(USB_Constants::AsyncCommand& aData, int aLength);
    void HandleAsynchronousData(USB_Constants::AsyncCommand& aData, int aLength);
    void HandleStitch(USB_Constants::AsyncCommand& aData, int aLength);
    int  SendHello();

    std::array<char, cMaxAsynchronousBuffer>             mAsyncReceiveBuffer{0};
    std::queue<std::array<char, cMaxAsynchronousBuffer>> mAsyncSendBuffer{};
    std::array<char, cMaxAsynchronousBuffer>             mPacketToSend{};
    std::mutex                                           mAsyncSendBufferMutex{};  //< 2 threads are accessing this.
    std::array<char, cMaxUSBBuffer>                      mBuffer{0};
    unsigned int                                         mBytesInAsyncBuffer{0};
    libusb_device_handle*                                mDeviceHandle{nullptr};
    bool                                                 mError{false};
    std::shared_ptr<XLinkKaiConnection>                  mIncomingConnection{nullptr};
    int                                                  mLength{0};
    std::shared_ptr<boost::thread>                       mReceiverThread{nullptr};
    int                                                  mRetryCounter{0};
    bool                                                 mStitching{false};  //< True: program starts stitching packets.
    int  mActualPacketLength{0};  //< returned by PSP when receiving a netpacket.
    bool mUSBCheckSuccessful{false};
};

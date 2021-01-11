#pragma once

/* Copyright (c) 2020 [Rick de Bondt] - USBReader.h
 *
 * This file contains the header for a USBReader class which will be used to read/write data from/to the PSP.
 *
 * */

#include <array>
#include <iostream>
#include <string_view>

#include <boost/thread.hpp>

#include <libusb.h>

constexpr int cMaxAsynchronousBuffer = 4096;

class XLinkKaiConnection;
class USBReader
{
public:
    /**
     * Opens the the USB device, so we can send/read data.
     * @return true if successful.
     */
    bool OpenDevice();

    /**
     * Closes the device
     */
    void CloseDevice();

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
     * Sends a Bulk In request on the USB bus.
     * @param aEndpoint - The endpoint to use.
     * @param aSize - Size of data to send.
     * @param aTimeout - The timeout of the request to set.
     * @return Amount of bytes read, < 0 on error .
     */
    int USBBulkRead(int aEndpoint, int aSize, int aTimeOut);

    bool StartReceiverThread();

    /**
     * Handles traffic from USB.
     */
    void ReceiveCallback();


    void SetIncomingConnection(std::shared_ptr<XLinkKaiConnection> aDevice);

private:
    bool USBCheckDevice();
    int  SendHello();

    bool                                              mError{false};
    int                                               mRetryCounter{0};
    std::shared_ptr<boost::thread>                    mReceiverThread{nullptr};
    std::shared_ptr<XLinkKaiConnection>               mIncomingConnection{nullptr};
    libusb_device_handle*                             mDeviceHandle = nullptr;
    std::array<unsigned char, cMaxAsynchronousBuffer> mBuffer{};
    int                                               mLength{0};
};

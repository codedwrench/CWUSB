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

#include "../Includes/USBConstants.h"

constexpr int cMaxUSBBuffer = 512;

// The maximum 802.11 MTU is 2304 bytes. 802.11-2012, page 413, section 8.3.2.1
constexpr int cMaxAsynchronousBuffer = 2304;

using ArrayWithLength = std::pair<std::array<char, cMaxAsynchronousBuffer>, int>;

struct libusb_device_handle;
class XLinkKaiConnection;

class USBReader
{
public:
    USBReader();
    ~USBReader();
    USBReader(const USBReader& aUSBReader) = delete;
    USBReader& operator=(const USBReader& aUSBReader) = delete;

    /**
     * Closes the device
     */
    void Close();

    /**
     * Opens the the USB device, so we can send/read data.
     * @return true if successful.
     */
    bool Open();

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
    int USBBulkWrite(int aEndpoint, char* aData, int aSize, int aTimeOut);

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
    void HandleAsynchronousSend();
    void HandleClose();
    void HandleError();
    void HandleStitch(USB_Constants::AsyncCommand& aData, int aLength);
    int  SendHello();

    /** True: program starts stitching packets. **/
    bool                            mReceiveStitching{false};
    ArrayWithLength                 mAsyncReceiveBuffer{};
    std::array<char, cMaxUSBBuffer> mTemporaryReceiveBuffer{0};

    bool mAwaitClose{false};

    /** True: program starts stitching packets. **/
    bool                        mSendStitching{false};
    std::queue<ArrayWithLength> mAsyncSendBuffer{};
    std::mutex                  mAsyncSendBufferMutex{};  //< 2 threads are accessing this.
    ArrayWithLength             mPacketToSend{};
    unsigned int                mBytesSent{0};

    libusb_device_handle*               mDeviceHandle{nullptr};
    bool                                mError{false};
    std::shared_ptr<XLinkKaiConnection> mIncomingConnection{nullptr};
    int                                 mLength{0};
    std::shared_ptr<boost::thread>      mReceiverThread{nullptr};
    int                                 mRetryCounter{0};

    int  mActualPacketLength{0};  //< returned by PSP when receiving a netpacket.
    bool mUSBCheckSuccessful{false};
};

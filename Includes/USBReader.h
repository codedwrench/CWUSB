#pragma once

/* Copyright (c) 2021 [Rick de Bondt] - USBReader.h
 *
 * This file contains the header for a USBReader class which will be used to read/write data from/to the PSP.
 *
 * */

#include <array>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string_view>

#include "USBConstants.h"

namespace boost
{
    class thread;
}

struct libusb_device_handle;
class USBReceiveThread;
class USBSendThread;
class XLinkKaiConnection;

class USBReader
{
public:
    USBReader(int aMaxBufferedMessages,
              int aMaxFatalRetries,
              int aMaxReadWriteRetries,
              int aReadTimeoutMS,
              int aWriteTimeoutMS);
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
    void HandleAsynchronousSend();
    void HandleClose();
    void HandleError();
    void HandleStitch(USB_Constants::AsyncCommand& aData, int aLength);
    int  SendHello();

    int mMaxBufferedMessages{0};
    int mMaxFatalRetries{0};
    int mMaxReadWriteRetries{0};
    int mReadTimeoutMS{0};
    int mWriteTimeoutMS{0};

    int mReadWriteRetryCounter{0};

    /** True: program starts stitching packets. **/
    bool                                               mReceiveStitching{false};
    std::array<char, USB_Constants::cMaxUSBPacketSize> mTemporaryReceiveBuffer{0};

    bool mStopRequest{false};

    /** True: program starts stitching packets. **/
    bool mSendStitching{false};

    libusb_device_handle*               mDeviceHandle{nullptr};
    bool                                mError{false};
    std::shared_ptr<XLinkKaiConnection> mIncomingConnection{nullptr};
    int                                 mLength{0};
    int                                 mActualLength{0};
    int                                 mStitchingLength{0};
    std::shared_ptr<boost::thread>      mUSBThread{nullptr};
    std::shared_ptr<USBReceiveThread>   mUSBReceiveThread{nullptr};
    std::shared_ptr<USBSendThread>      mUSBSendThread{nullptr};

    int mRetryCounter{0};

    bool mUSBCheckSuccessful{false};
};

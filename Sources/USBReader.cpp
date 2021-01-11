#include "../Includes/USBReader.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <sys/types.h>

#include <unistd.h>

#include "../Includes/Logger.h"
#include "libusb.h"

#if defined(_MSC_VER) && defined(__MINGW32__)
#include <hidclass.h>
#endif

namespace
{
    namespace eUSBResetStatus
    {
        enum Status
        {
            Closing,
            Resetting,
            Opening,
            Opened,
        };
    }  // namespace eUSBResetStatus

    namespace eUSBResetDeviceStatus
    {
        enum Status
        {
            None,
            Resetting,
        };
    }  // namespace eUSBResetDeviceStatus

    const std::vector<std::string_view> cUsbResetStatus{
        "USB_RESET_STATUS_CLOSING",
        "USB_RESET_STATUS_RESETTING",
        "USB_RESET_STATUS_OPENING",
        "USB_RESET_STATUS_OPENED",
    };

    constexpr int cPSPVID = 0x054;
    constexpr int cPSPPID = 0x1C9;


#if defined(_MSC_VER) && defined(__MINGW32__)
    const GUID GUID_D EVINTERFACE_USB_DEVICE = {
        0xA5DCBF10L, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

    static std::unique_ptr<HDEVNOTIFY, DeviceNotificationDeleter> pspDeviceNotify;
#endif

}  // namespace

using namespace std::chrono_literals;

bool USBReader::OpenDevice(libusb_device** aDevices)
{
    libusb_device_handle* lDeviceHandle{nullptr};
    libusb_device*        lDevice{nullptr};
    int                   lCount{0};

    for (lDevice = aDevices[lCount]; lDevice != nullptr; lCount++) {
        libusb_device_descriptor lDescriptor{};
        int                      lReturn = libusb_get_device_descriptor(lDevice, &lDescriptor);

        if (lReturn >= 0) {
            if ((lDescriptor.idVendor == cPSPVID) && (lDescriptor.idProduct == cPSPPID)) {
                int lReturn = libusb_open(lDevice, &lDeviceHandle);
                if (lReturn >= 0 && lDeviceHandle != nullptr) {
                    int lReturn = libusb_set_configuration(lDeviceHandle, 1);

                    if (lReturn >= 0) {
                        lReturn = libusb_claim_interface(lDeviceHandle, 0);
                        if (lReturn == 0) {
                            mDeviceHandle = lDeviceHandle;
                        } else {
                            Logger::GetInstance().Log(
                                std::string("Could not detach kernel driver: ") + libusb_strerror(lReturn),
                                Logger::Level::ERROR);
                            libusb_close(lDeviceHandle);
                        }
                    } else {
                        Logger::GetInstance().Log(std::string("Could set configuration: ") + libusb_strerror(lReturn),
                                                  Logger::Level::ERROR);
                        libusb_close(lDeviceHandle);
                    }
                } else {
                    Logger::GetInstance().Log(std::string("Could not open USB device") + libusb_strerror(lReturn),
                                              Logger::Level::ERROR);
                }
            } else {
                Logger::GetInstance().Log(std::string("Non matching device found") +
                                              std::to_string(lDescriptor.idVendor) +
                                              std::to_string(lDescriptor.idProduct),
                                          Logger::Level::TRACE);
            }
        } else {
            Logger::GetInstance().Log(std::string("Cannot query device descriptor") + libusb_strerror(lReturn),
                                      Logger::Level::ERROR);
            libusb_free_device_list(aDevices, 1);
        }
    }

    return (mDeviceHandle != nullptr);
}


void USBReader::CloseDevice()
{
    if (mDeviceHandle != nullptr) {
        libusb_release_interface(mDeviceHandle, 0);
        libusb_reset_device(mDeviceHandle);
        libusb_close(mDeviceHandle);
        mDeviceHandle = nullptr;
    }
}


int USBReader::USBBulkWrite(int aEndpoint, std::string_view aData, int aSize, int aTimeOut)
{
    int lReturn{-1};

    if (mDeviceHandle != nullptr) {
        Logger::GetInstance().Log(
            std::string("Bulk Write, size: ") + std::to_string(aSize) + " , timeout: " + std::to_string(aTimeOut),
            Logger::Level::ERROR);

        int lReturn{0};

        // Copy to a vector temporarily so we have a non-const unsigned char array
        std::vector<unsigned char> lString{aData.begin(), aData.end()};
        libusb_bulk_transfer(mDeviceHandle, aEndpoint, lString.data(), aSize, &lReturn, aTimeOut);
        Logger::GetInstance().Log(std::string("Bulk write returned :") + std::to_string(lReturn), Logger::Level::ERROR);
    }

    return lReturn;
}

int USBReader::USBBulkRead(int aEndpoint, int aSize, int aTimeOut)
{
    int lReturn{-1};
    if (mDeviceHandle != nullptr) {
        Logger::GetInstance().Log(
            std::string("Bulk Read, size: ") + std::to_string(aSize) + " , timeout: " + std::to_string(aTimeOut),
            Logger::Level::ERROR);

        int lReturn{0};
        libusb_bulk_transfer(mDeviceHandle, aEndpoint, mBuffer.data(), aSize, &lReturn, aTimeOut);

        Logger::GetInstance().Log(std::string("Bulk write returned :") + std::to_string(lReturn), Logger::Level::ERROR);
    }

    return lReturn;
}

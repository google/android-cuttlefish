/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "sysdeps.h"
#include "transport.h"
#include "types.h"

#include <stdint.h>
#include <optional>
#include <string>

#include "libusb/libusb.h"

// A session is started when a device is connected to a workstation. It ends upon its
// disconnection. For in-house hotplug, we generate a unique identifier based on the device
// invariants vendor, product (adb vs mtp...), the USB port, and the address (the location
// in the USB chain). On Windows, the address is always incremented, even if the same device
// is unplugged and plugged immediately.
union USBSessionID {
    uint64_t id;
    struct {
        uint8_t address;
        uint8_t port;
        uint16_t product;
        uint16_t vendor;
    } fields;
};

// Abstraction layer simplifying libusb_device management
struct LibUsbDevice {
  public:
    explicit LibUsbDevice(libusb_device* device);
    ~LibUsbDevice();

    // Device must have been Opened prior to calling this method.
    // This method blocks until a packet is available on the USB.
    // Calling Close will make it return even if not packet was
    // read.
    bool Read(apacket* packet);

    // Device must have been Opened prior to calling this method.
    // This method blocks until the packet has been submitted to
    // the USB.
    bool Write(apacket* packet);

    // Reset the device. This will cause the OS to issue a disconnect
    // and the device will re-connect.
    void Reset();

    uint64_t NegotiatedSpeedMbps();
    uint64_t MaxSpeedMbps();

    // Return the Android serial
    std::string GetSerial();

    // Acquire all resources necessary for USB transfer.
    bool Open();

    // Release all resources necessary for USB transfer.
    bool Close();

    // Get the OS address (e.g.: usb:4.0.1)
    std::string GetAddress() const;

    // Call immediately after creating this object to check that the device can be interacted
    // with (this also makes sure this is an Android device).
    bool IsInitialized() const;

    USBSessionID GetSessionId() const;

    static USBSessionID GenerateSessionId(libusb_device* device);

    // Clears halt condition for endpoints
    void ClearEndpoints();

  private:
    // Make sure device is and Android device, retrieve OS address, retrieve Android serial.
    void Init();

    std::optional<libusb_device_descriptor> GetDeviceDescriptor();

    bool ClaimInterface();
    void ReleaseInterface();

    bool OpenDeviceHandle();
    void CloseDeviceHandle();

    void CloseDevice();
    std::string GetDeviceAddress();
    bool RetrieveSerial();
    void RetrieveSpeeds();

    bool FindAdbInterface();

    libusb_device* device_ = nullptr;
    libusb_device_handle* device_handle_ = nullptr;
    std::string device_address_{};
    std::string serial_{};

    // The mask used to determine if we should send a Zero Length Packet
    int zlp_mask_{};
    int out_endpoint_size_{};

    int interface_num_ = 0;
    unsigned char write_endpoint_{};
    unsigned char read_endpoint_{};
    std::atomic<bool> interface_claimed_ = false;

    uint64_t negotiated_speed_{};
    uint64_t max_speed_{};

    bool initialized_ = false;

    USBSessionID session_;
};
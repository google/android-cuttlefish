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

#include "usb_libusb_device.h"

#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <libusb/libusb.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/thread_annotations.h>

#include "adb.h"
#include "adb_trace.h"
#include "adb_utils.h"
#include "fdevent/fdevent.h"
#include "transport.h"
#include "usb.h"

using namespace std::chrono_literals;

using android::base::ScopedLockAssertion;
using android::base::StringPrintf;

static bool endpoint_is_output(uint8_t endpoint) {
    return (endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT;
}

LibUsbDevice::LibUsbDevice(libusb_device* device)
    : device_(device), device_address_(GetDeviceAddress()) {
    libusb_ref_device(device);
    Init();
}

LibUsbDevice::~LibUsbDevice() {
    ReleaseInterface();
    CloseDeviceHandle();
    CloseDevice();
}

bool LibUsbDevice::IsInitialized() const {
    return initialized_;
}

void LibUsbDevice::Init() {
    initialized_ = OpenDeviceHandle();
    session_ = GenerateSessionId(device_);
}

void LibUsbDevice::ReleaseInterface() {
    if (interface_claimed_) {
        libusb_release_interface(device_handle_, interface_num_);
        interface_claimed_ = false;
    }
}

void LibUsbDevice::CloseDeviceHandle() {
    if (device_handle_ != nullptr) {
        libusb_close(device_handle_);
        device_handle_ = nullptr;
    }
}

void LibUsbDevice::CloseDevice() {
    if (device_ != nullptr) {
        libusb_unref_device(device_);
        device_ = nullptr;
    }
}

bool LibUsbDevice::Write(apacket* packet) {
    VLOG(USB) << "Write " << command_to_string(packet->msg.command)
              << " payload=" << packet->msg.data_length;
    int transferred;
    int data_size = sizeof(packet->msg);
    auto r = libusb_bulk_transfer(device_handle_, write_endpoint_, (unsigned char*)&packet->msg,
                                  data_size, &transferred, 0);
    if ((r != 0) || (transferred != data_size)) {
        VLOG(USB) << "LibUsbDevice::Write failed at header " << libusb_error_name(r);
        return false;
    }

    data_size = packet->payload.size();
    if (data_size == 0) {
        return true;
    }
    r = libusb_bulk_transfer(device_handle_, write_endpoint_,
                             (unsigned char*)packet->payload.data(), data_size, &transferred, 0);
    if ((r != 0) || (transferred != data_size)) {
        VLOG(USB) << "LibUsbDevice::Write failed at payload " << libusb_error_name(r);
        return false;
    }

    if ((data_size & zlp_mask_) == 0) {
        VLOG(USB) << "Sending zlp (payload_size=" << data_size
                  << ", endpoint_size=" << out_endpoint_size_
                  << ", modulo=" << data_size % out_endpoint_size_ << ")";
        libusb_bulk_transfer(device_handle_, write_endpoint_,
                             (unsigned char*)packet->payload.data(), 0, &transferred, 0);
    }

    return true;
}

bool LibUsbDevice::Read(apacket* packet) {
    VLOG(USB) << "LibUsbDevice Read()";
    int transferred;
    int data_size = sizeof(packet->msg);
    auto r = libusb_bulk_transfer(device_handle_, read_endpoint_, (unsigned char*)&packet->msg,
                                  data_size, &transferred, 0);
    if ((r != 0) || (transferred != data_size)) {
        VLOG(USB) << "LibUsbDevice::READ failed at header " << libusb_error_name(r);
        return false;
    }
    VLOG(USB) << "Read " << command_to_string(packet->msg.command)
              << " header, now expecting=" << packet->msg.data_length;
    if (packet->msg.data_length == 0) {
        packet->payload.resize(0);
        return true;
    }

    packet->payload.resize(packet->msg.data_length);
    data_size = packet->msg.data_length;
    r = libusb_bulk_transfer(device_handle_, read_endpoint_, (unsigned char*)packet->payload.data(),
                             data_size, &transferred, 0);
    if ((r != 0) || (transferred != data_size)) {
        VLOG(USB) << "LibUsbDevice::READ failed at payload << " << libusb_error_name(r);
        return false;
    }
    VLOG(USB) << "Read " << command_to_string(packet->msg.command) << " got =" << transferred;

    return true;
}

void LibUsbDevice::Reset() {
    if (device_handle_ == nullptr) {
        return;
    }
    int rc = libusb_reset_device(device_handle_);
    if (rc != 0) {
        LOG(ERROR) << "libusb_reset_device failed: " << libusb_error_name(rc);
    }
}

std::string LibUsbDevice::GetDeviceAddress() {
    uint8_t ports[7];
    int port_count = libusb_get_port_numbers(device_, ports, 7);
    if (port_count < 0) return "";

    std::string address =
            android::base::StringPrintf("%d-%d", libusb_get_bus_number(device_), ports[0]);
    for (int port = 1; port < port_count; ++port) {
        address += android::base::StringPrintf(".%d", ports[port]);
    }

    return address;
}

std::optional<libusb_device_descriptor> LibUsbDevice::GetDeviceDescriptor() {
    libusb_device_descriptor device_desc;
    int rc = libusb_get_device_descriptor(device_, &device_desc);
    if (rc != 0) {
        LOG(WARNING) << "failed to get device descriptor for device :" << libusb_error_name(rc);
        return {};
    }
    return device_desc;
}

std::string LibUsbDevice::GetSerial() {
    return serial_;
}

bool LibUsbDevice::FindAdbInterface() {
    std::optional<libusb_device_descriptor> device_desc = GetDeviceDescriptor();
    if (!device_desc.has_value()) {
        return false;
    }

    if (device_desc->bDeviceClass != LIBUSB_CLASS_PER_INTERFACE) {
        // Assume that all Android devices have the device class set to per interface.
        // TODO: Is this assumption valid?
        VLOG(USB) << "skipping device with incorrect class at " << device_address_;
        return false;
    }

    libusb_config_descriptor* config;
    int rc = libusb_get_active_config_descriptor(device_, &config);
    if (rc != 0) {
        LOG(WARNING) << "failed to get active config descriptor for device at " << device_address_
                     << ": " << libusb_error_name(rc);
        return false;
    }

    // Use size_t for interface_num so <iostream>s don't mangle it.
    size_t interface_num;
    uint8_t bulk_in = 0, bulk_out = 0;
    size_t packet_size = 0;
    bool found_adb = false;

    for (interface_num = 0; interface_num < config->bNumInterfaces; ++interface_num) {
        const libusb_interface& interface = config->interface[interface_num];

        if (interface.num_altsetting == 0) {
            continue;
        }

        const libusb_interface_descriptor& interface_desc = interface.altsetting[0];
        if (!is_adb_interface(interface_desc.bInterfaceClass, interface_desc.bInterfaceSubClass,
                              interface_desc.bInterfaceProtocol)) {
            VLOG(USB) << "skipping non-adb interface at " << device_address_ << " (interface "
                      << interface_num << ")";
            continue;
        }

        VLOG(USB) << "found potential adb interface at " << device_address_ << " (interface "
                  << interface_num << ")";

        bool found_in = false;
        bool found_out = false;
        for (size_t endpoint_num = 0; endpoint_num < interface_desc.bNumEndpoints; ++endpoint_num) {
            const auto& endpoint_desc = interface_desc.endpoint[endpoint_num];
            const uint8_t endpoint_addr = endpoint_desc.bEndpointAddress;
            const uint8_t endpoint_attr = endpoint_desc.bmAttributes;
            VLOG(USB) << "Scanning endpoint=" << endpoint_num
                      << ", addr=" << std::format("{:#02x}", endpoint_addr)
                      << ", attr=" << std::format("{:#02x}", endpoint_attr);

            const uint8_t transfer_type = endpoint_attr & LIBUSB_TRANSFER_TYPE_MASK;

            if (transfer_type != LIBUSB_TRANSFER_TYPE_BULK) {
                continue;
            }

            if (endpoint_is_output(endpoint_addr) && !found_out) {
                found_out = true;
                out_endpoint_size_ = endpoint_desc.wMaxPacketSize;
                VLOG(USB) << "Device " << GetSerial()
                          << " uses wMaxPacketSize=" << out_endpoint_size_;
                zlp_mask_ = out_endpoint_size_ - 1;
                bulk_out = endpoint_addr;
            } else if (!endpoint_is_output(endpoint_addr) && !found_in) {
                found_in = true;
                bulk_in = endpoint_addr;
            }

            size_t endpoint_packet_size = endpoint_desc.wMaxPacketSize;
            CHECK(endpoint_packet_size != 0);
            if (packet_size == 0) {
                packet_size = endpoint_packet_size;
            } else {
                CHECK(packet_size == endpoint_packet_size);
            }
        }

        if (found_in && found_out) {
            found_adb = true;
            break;
        } else {
            VLOG(USB) << "rejecting potential adb interface at " << device_address_ << "(interface "
                      << interface_num << "): missing bulk endpoints "
                      << "(found_in = " << found_in << ", found_out = " << found_out << ")";
        }
    }

    libusb_free_config_descriptor(config);

    if (!found_adb) {
        VLOG(USB) << "ADB interface missing endpoints: bulk_out=" << bulk_out
                  << " and bulk_in=" << bulk_in;
        return false;
    }

    interface_num_ = interface_num;
    write_endpoint_ = bulk_out;
    read_endpoint_ = bulk_in;

    VLOG(USB) << "Found ADB interface=" << interface_num_
              << " bulk_in=" << std::format("{:#02x}", bulk_in)
              << " bulk_out=" << std::format("{:#02x}", bulk_out);
    return true;
}

std::string LibUsbDevice::GetAddress() const {
    return std::string("usb:") + device_address_;
}

bool LibUsbDevice::RetrieveSerial() {
    auto device_desc = GetDeviceDescriptor();

    serial_.resize(512);
    int rc = libusb_get_string_descriptor_ascii(device_handle_, device_desc->iSerialNumber,
                                                reinterpret_cast<unsigned char*>(&serial_[0]),
                                                serial_.length());
    if (rc == 0) {
        LOG(WARNING) << "received empty serial from device at " << device_address_;
        return false;
    } else if (rc < 0) {
        VLOG(USB) << "failed to get serial from device " << device_address_ << " :"
                  << libusb_error_name(rc);
        return false;
    }
    serial_.resize(rc);
    return true;
}

// Clear halt condition for endpoints
void LibUsbDevice::ClearEndpoints() {
    if (device_handle_ == nullptr) {
        VLOG(USB) << "cannot clear device endpoints, invalid device handle";
        return;
    }

    if (!interface_claimed_) {
        VLOG(USB) << "cannot clear device endpoints, adb interface not claimed";
        return;
    }

    for (uint8_t endpoint : {read_endpoint_, write_endpoint_}) {
        int rc = libusb_clear_halt(device_handle_, endpoint);
        if (rc != 0) {
            VLOG(USB) << "failed to clear halt on device " << serial_ << " endpoint "
                      << StringPrintf("%#x", endpoint) << ": " << libusb_error_name(rc);
        }
    }
}

// libusb gives us an int which is a value from 'enum libusb_speed'
static uint64_t ToConnectionSpeed(int speed) {
    switch (speed) {
        case LIBUSB_SPEED_LOW:
            return 1;
        case LIBUSB_SPEED_FULL:
            return 12;
        case LIBUSB_SPEED_HIGH:
            return 480;
        case LIBUSB_SPEED_SUPER:
            return 5000;
        case LIBUSB_SPEED_SUPER_PLUS:
            return 10000;
        case LIBUSB_SPEED_SUPER_PLUS_X2:
            return 20000;
        case LIBUSB_SPEED_UNKNOWN:
        default:
            return 0;
    }
}

// libusb gives us a bitfield made of 'enum libusb_supported_speed' values
static uint64_t ExtractMaxSuperSpeed(uint16_t wSpeedSupported) {
    if (wSpeedSupported == 0) {
        return 0;
    }

    int msb = 0;
    while (wSpeedSupported >>= 1) {
        msb++;
    }

    switch (1 << msb) {
        case LIBUSB_LOW_SPEED_OPERATION:
            return 1;
        case LIBUSB_FULL_SPEED_OPERATION:
            return 12;
        case LIBUSB_HIGH_SPEED_OPERATION:
            return 480;
        case LIBUSB_SUPER_SPEED_OPERATION:
            return 5000;
        default:
            return 0;
    }
}

static uint64_t ExtractMaxSuperSpeedPlus(libusb_ssplus_usb_device_capability_descriptor* cap) {
    // The exponents is one of {bytes, kB, MB, or GB}. We express speed in MB so we use a 0
    // multiplier for value which would result in 0MB anyway.
    static uint64_t exponent[] = {0, 0, 1, 1000};
    uint64_t max_speed = 0;
    for (uint8_t i = 0; i < cap->numSublinkSpeedAttributes; i++) {
        libusb_ssplus_sublink_attribute* attr = &cap->sublinkSpeedAttributes[i];
        uint64_t speed = attr->mantissa * exponent[attr->exponent];
        max_speed = std::max(max_speed, speed);
    }
    return max_speed;
}

void LibUsbDevice::RetrieveSpeeds() {
    negotiated_speed_ = ToConnectionSpeed(libusb_get_device_speed(device_));

    // To discover the maximum speed supported by an USB device, we walk its capability
    // descriptors.
    struct libusb_bos_descriptor* bos = nullptr;
    if (libusb_get_bos_descriptor(device_handle_, &bos)) {
        return;
    }

    for (int i = 0; i < bos->bNumDeviceCaps; i++) {
        switch (bos->dev_capability[i]->bDevCapabilityType) {
            case LIBUSB_BT_SS_USB_DEVICE_CAPABILITY: {
                libusb_ss_usb_device_capability_descriptor* cap = nullptr;
                if (!libusb_get_ss_usb_device_capability_descriptor(nullptr, bos->dev_capability[i],
                                                                    &cap)) {
                    max_speed_ = std::max(max_speed_, ExtractMaxSuperSpeed(cap->wSpeedSupported));
                    libusb_free_ss_usb_device_capability_descriptor(cap);
                }
            } break;
            case LIBUSB_BT_SUPERSPEED_PLUS_CAPABILITY: {
                libusb_ssplus_usb_device_capability_descriptor* cap = nullptr;
                if (!libusb_get_ssplus_usb_device_capability_descriptor(
                            nullptr, bos->dev_capability[i], &cap)) {
                    max_speed_ = std::max(max_speed_, ExtractMaxSuperSpeedPlus(cap));
                    libusb_free_ssplus_usb_device_capability_descriptor(cap);
                }
            } break;
            default:
                break;
        }
    }
    libusb_free_bos_descriptor(bos);
}

bool LibUsbDevice::OpenDeviceHandle() {
    if (device_handle_) {
        VLOG(USB) << "device already open";
        return true;
    }

    int rc = libusb_open(device_, &device_handle_);
    if (rc != 0) {
        VLOG(USB) << "Unable to open device: " << GetSerial() << " :" << libusb_strerror(rc);
        return false;
    }

    if (!RetrieveSerial()) {
        return false;
    }

    if (!FindAdbInterface()) {
        return false;
    }

    RetrieveSpeeds();
    return true;
}

bool LibUsbDevice::ClaimInterface() {
    VLOG(USB) << "ClaimInterface for " << GetSerial();
    if (interface_claimed_) {
        VLOG(USB) << "Interface already open";
        return true;
    }

    if (!FindAdbInterface()) {
        VLOG(USB) << "Unable to open interface for " << GetSerial();
        return false;
    }

    int rc = libusb_claim_interface(device_handle_, interface_num_);
    if (rc != 0) {
        VLOG(USB) << "failed to claim adb interface for device " << serial_.c_str() << ":"
                  << libusb_error_name(rc);
        return false;
    }

    VLOG(USB) << "Claimed interface for " << GetSerial() << ", "
              << StringPrintf("bulk_in = %#x, bulk_out = %#x", read_endpoint_, write_endpoint_);
    interface_claimed_ = true;
    return true;
}

bool LibUsbDevice::Open() {
    if (!OpenDeviceHandle()) {
        VLOG(USB) << "Unable to attach, cannot open device";
        return false;
    }

    if (!ClaimInterface()) {
        VLOG(USB) << "failed to claim interface " << GetSerial();
        return false;
    }

    VLOG(USB) << "Attached device " << GetSerial();
    return true;
}

bool LibUsbDevice::Close() {
    ReleaseInterface();
    CloseDeviceHandle();
    return true;
}

uint64_t LibUsbDevice::MaxSpeedMbps() {
    return max_speed_;
}

uint64_t LibUsbDevice::NegotiatedSpeedMbps() {
    return negotiated_speed_;
}

USBSessionID LibUsbDevice::GenerateSessionId(libusb_device* dev) {
    libusb_device_descriptor desc{};
    auto result = libusb_get_device_descriptor(dev, &desc);
    if (result != LIBUSB_SUCCESS) {
        LOG(WARNING) << "Unable to retrieve device descriptor: " << libusb_error_name(result);
        return USBSessionID{};
    }

    USBSessionID session{};
    session.fields.vendor = desc.idVendor;
    session.fields.product = desc.idProduct;
    session.fields.port = libusb_get_port_number(dev);
    session.fields.address = libusb_get_device_address(dev);
    return session;
}

USBSessionID LibUsbDevice::GetSessionId() const {
    return session_;
}

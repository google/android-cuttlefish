/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <glog/logging.h>
#include <stdint.h>

#include "common/libs/fs/shared_fd.h"

// Requests and constants below are defined in kernel documentation file:
// https://www.kernel.org/doc/Documentation/usb/usbip_protocol.txt
namespace vadb {
namespace usbip {
namespace internal {
// Rotate endianness of the data to match protocol.
template <typename T>
void HostToNet(T* data);
template <typename T>
void NetToHost(T* data);
}  // namespace internal

// This is the range of USB/IP versions in which we should be safe to operate.
// USB/IP expects (and the expectation is strong) that the version reported by
// server is *same* as version reported by client, so we have to mock this for
// every client.
// TODO(ender): find if this is documented anywhere.
constexpr int kMinVersion = 0x100;  // 1.0.0
constexpr int kMaxVersion = 0x111;  // 1.1?.1?

// Send message to USB/IP client.
// Accept data by value and modify it to match net endian locally.
// Returns true, if message was sent successfully.
template <typename T>
bool SendUSBIPMsg(const avd::SharedFD& fd, T data) {
  internal::HostToNet(&data);
  return fd->Send(&data, sizeof(T), MSG_NOSIGNAL) == sizeof(T);
}

// Receive message from USB/IP client.
// After message is received, it's updated to match host endian.
// Returns true, if message was received successfully.
template <typename T>
bool RecvUSBIPMsg(const avd::SharedFD& fd, T* data) {
  VLOG(2) << "Reading " << sizeof(T) << " bytes...";
  bool res = fd->Recv(data, sizeof(T), MSG_NOSIGNAL) == sizeof(T);
  if (res) {
    internal::NetToHost(data);
  }
  return res;
}

////////////////////////////////////////////////////////////////////////////////
// OPERATIONS
////////////////////////////////////////////////////////////////////////////////

// Operation numbers. Operations are valid only when USB device is detached.
enum Operation : uint16_t {
  // Request import (~attach) USB device. Request data format:
  // - OpReqRepBusId - describing BUS ID.
  kUsbIpOpReqImport = 0x8003,

  // Import (~attach) response. Response format:
  // - OpRepDeviceInfo - USBIP device descriptor.
  kUsbIpOpRepImport = 3,

  // Request list available devices. No request data.
  kUsbIpOpReqDevList = 0x8005,

  // Device list response. Response format:
  // - OpRepDeviceListInfo - specifies number of device list reports that follow
  //   (n),
  // - n * OpRepDeviceInfo - USBIP device descriptor, including # interfaces
  //   (m),
  // - m * OpRepInterfaceInfo - for every OpRepDeviceInfo, list of interfaces.
  kUsbIpOpRepDevList = 5,
};

// Header precedes all OPERATION requests and responses.
// Header does NOT precede COMMAND requests and responses.
struct OpHeader {
  uint16_t version;   // BCD. Server must obey client, not the other way around.
  Operation command;  // Request or response type.
  uint32_t status;    // Status; 0 = ok, 1 = error.
};

// OPERATION request/response body is essentially several structures glued
// together. Because half of these messages are nested arrays of arbitrary
// lengths each, we can't define a single structure explaining full request or
// response body. Instead we will define components that, when combined,
// constitute this body.
//
// OpReqRepBusId functions both as a device info field and request body.
using OpReqRepBusId = char[32];

// OpRepDeviceListInfo is a header preceding an array of OpRepDeviceInfo devices
// offered by this server.
struct OpRepDeviceListInfo {
  uint32_t num_exported_devices;
} __attribute__((packed));

// OpRepDeviceInfo is used both as a partial response to OpReqDeviceList and
// OpReqImport. Depending on operation type it may or may not be followed by an
// array of OpRepInterfaceInfo interfaces this device exports.
struct OpRepDeviceInfo {
  char usb_path[256];
  OpReqRepBusId bus_id;
  uint32_t bus_num;
  uint32_t dev_num;
  uint32_t speed;
  uint16_t id_vendor;
  uint16_t id_product;
  uint16_t bcd_device;
  uint8_t device_class;
  uint8_t device_subclass;
  uint8_t device_protocol;
  uint8_t configuration_value;
  uint8_t num_configurations;
  uint8_t num_interfaces;
} __attribute__((packed));

// OpRepInterfaceInfo lists interface details of a particular USB device.
struct OpRepInterfaceInfo {
  uint8_t iface_class;
  uint8_t iface_subclass;
  uint8_t iface_protocol;
  uint8_t reserved;
} __attribute__((packed));

////////////////////////////////////////////////////////////////////////////////
// COMMANDS
////////////////////////////////////////////////////////////////////////////////

// Command numbers. Commands are valid only once USB device is attached.
enum Command : uint32_t {
  kUsbIpCmdReqSubmit = 1,  // Submit request
  kUsbIpCmdReqUnlink = 2,  // Unlink request
  kUsbIpCmdRepSubmit = 3,  // Submit response
  kUsbIpCmdRepUnlink = 4,  // Unlink response
};

// Direction of data flow.
enum Direction : uint32_t {
  kUsbIpDirectionOut = 0,
  kUsbIpDirectionIn = 1,
};

// Setup structure is explained in great detail here:
// - http://www.beyondlogic.org/usbnutshell/usb6.shtml
// - http://www.usbmadesimple.co.uk/ums_4.htm
struct CmdRequest {
  uint8_t type;
  uint8_t cmd;
  uint16_t value;
  uint16_t index;
  uint16_t length;
} __attribute__((packed));

// CmdHeader precedes any command request or response body.
struct CmdHeader {
  Command command;
  uint32_t seq_num;
  uint16_t bus_num;
  uint16_t dev_num;
  Direction direction;
  uint32_t endpoint;  // valid values: 0-15
} __attribute__((packed));

// Command data for submitting an USB request.
struct CmdReqSubmit {
  uint32_t transfer_flags;
  uint32_t transfer_buffer_length;
  uint32_t start_frame;
  uint32_t number_of_packets;
  uint32_t deadline_interval;
  CmdRequest setup;
} __attribute__((packed));

// Command response for submitting an USB request.
struct CmdRepSubmit {
  uint32_t status;  // 0 = success.
  uint32_t actual_length;
  uint32_t start_frame;
  uint32_t number_of_packets;
  uint32_t error_count;
  CmdRequest setup;
} __attribute__((packed));

// Unlink USB request.
struct CmdReqUnlink {
  uint32_t seq_num;
  uint32_t reserved[6];
} __attribute__((packed));

// Unlink USB response.
struct CmdRepUnlink {
  uint32_t status;
  uint32_t reserved[6];
} __attribute__((packed));

// Diagnostics.
std::ostream& operator<<(std::ostream& out, Operation op);
std::ostream& operator<<(std::ostream& out, const OpHeader& header);
std::ostream& operator<<(std::ostream& out, const OpRepDeviceInfo& data);
std::ostream& operator<<(std::ostream& out, const OpRepDeviceListInfo& list);
std::ostream& operator<<(std::ostream& out, const OpRepInterfaceInfo& i);
std::ostream& operator<<(std::ostream& out, const CmdHeader& header);
std::ostream& operator<<(std::ostream& out, const CmdReqSubmit& data);
std::ostream& operator<<(std::ostream& out, const CmdRepSubmit& data);
std::ostream& operator<<(std::ostream& out, const CmdReqUnlink& data);
std::ostream& operator<<(std::ostream& out, const CmdRepUnlink& data);

}  // namespace usbip
}  // namespace vadb

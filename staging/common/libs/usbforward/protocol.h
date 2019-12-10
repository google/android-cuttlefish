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

#include <stdint.h>

namespace usb_forward {

// Commands that can be executed over serial port.
// Use magic value to avoid accidental interpretation of commonly seen numbers.
enum Command : uint32_t {
  // Get device list.
  // Request format:
  // - RequestHeader{}
  // Response format:
  // - ResponseHeader{}
  // - int32_t(num_devices)
  // - num_devices times:
  //   - DeviceInfo{}
  //   - DeviceInfo.num_interfaces times:
  //     - InterfaceInfo{}
  CmdDeviceList = 0xcfad0001,

  // Attach specified device.
  // Request format:
  // - RequestHeader{}
  // - AttachRequestHeader{}
  // Response format:
  // - ResponseHeader{}
  CmdAttach,

  // Execute command on attached USB device.
  // Request format:
  // - RequestHeader{}
  // - ControlTransfer{}
  // - if transfer direction is host -> device
  //   - uint8_t[ControlTransfer.length] data
  // Response format:
  // - ResponseHeader{}
  // - if transfer direction is device -> host
  //   - int32_t(actual length)
  //   - uint8_t[actual length] bytes
  CmdControlTransfer,

  // Execute transfer on attached USB device.
  // Request format:
  // - RequestHeader{}
  // - DataTransfer{}
  // - if transfer direction is host -> device
  //   - uint8_t[DataTransfer.length] data
  // Response format:
  // - ResponseHeader{}
  // - if transfer direction is host -> device
  //   - int32_t(actual length)
  //   - int32_t[actual length] bytes
  CmdDataTransfer,

  // Heartbeat is used to detect whether device is alive.
  // This is a trivial request/response mechanism.
  // Response status indicates whether server is ready.
  // Request format:
  // - RequestHeader{}
  // Response format:
  // - ResponseHeader{}
  CmdHeartbeat,
};

// Status represents command execution result, using USB/IP compatible values.
enum Status : uint32_t {
  // StatusSuccess indicates successful command execution.
  StatusSuccess = 0,

  // StatusFailure indicates error during command execution.
  StatusFailure = 1
};

struct RequestHeader {
  Command command;
  uint32_t tag;
};

struct ResponseHeader {
  Status status;
  uint32_t tag;
};

// DeviceInfo describes individual USB device that was found attached to the
// bus.
struct DeviceInfo {
  uint16_t vendor_id;
  uint16_t product_id;
  uint16_t dev_version;
  uint8_t dev_class;
  uint8_t dev_subclass;
  uint8_t dev_protocol;
  uint8_t bus_id;
  uint8_t dev_id;
  uint8_t speed;
  uint8_t num_configurations;
  uint8_t num_interfaces;
  uint8_t cur_configuration;
} __attribute__((packed));

// InterfaceInfo describes individual interface attached to a USB device.
struct InterfaceInfo {
  uint8_t if_class;
  uint8_t if_subclass;
  uint8_t if_protocol;
  uint8_t if_reserved;
} __attribute__((packed));

// AttachRequest specifies which device on which bus needs to be attached.
struct AttachRequest {
  uint8_t bus_id;
  uint8_t dev_id;
} __attribute__((packed));

// ControlTransfer specifies target bus and device along with USB request.
struct ControlTransfer {
  uint8_t bus_id;
  uint8_t dev_id;
  uint8_t type;
  uint8_t cmd;
  uint16_t value;
  uint16_t index;
  uint16_t length;
  uint32_t timeout;
} __attribute__((packed));

// DataTransfer is used to exchange data between host and device.
struct DataTransfer {
  uint8_t bus_id;
  uint8_t dev_id;
  uint8_t endpoint_id;
  uint8_t is_host_to_device;
  int32_t length;
  uint32_t timeout;
} __attribute__((packed));

}  // namespace usb_forward

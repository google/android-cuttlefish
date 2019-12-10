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

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "host/commands/virtual_usb_manager/usbip/messages.h"

namespace vadb {
namespace usbip {

// The device descriptor of a USB device represents a USB device that is
// available for import.
class Device {
 public:
  // AsyncTransferReadyCB specifies a signature of a function that will be
  // called upon transfer completion (whether successful or failed). Parameters
  // supplied to the function are:
  // - operation status, indicated by boolean flag (true = success),
  // - vector containing transferred data (and actual size).
  using AsyncTransferReadyCB = std::function<void(bool, std::vector<uint8_t>)>;

  // Interface provides minimal description of device's interface.
  struct Interface {
    uint8_t iface_class;
    uint8_t iface_subclass;
    uint8_t iface_protocol;
  };

  // vendor_id and product_id identify device manufacturer and type.
  // dev_version describes device version (as BCD).
  uint16_t vendor_id;
  uint16_t product_id;
  uint16_t dev_version;

  // Class, Subclass and Protocol define device type.
  uint8_t dev_class;
  uint8_t dev_subclass;
  uint8_t dev_protocol;

  // Speed indicates device speed (see libusb_speed).
  uint8_t speed;

  // ConfigurationsCount and ConfigurationNumber describe total number of device
  // configurations and currently activated device configuration.
  size_t configurations_count;
  size_t configuration_number;

  // Interfaces returns a collection of device interfaces.
  std::vector<Interface> interfaces;

  // Attach request handler.
  std::function<bool()> handle_attach;

  // Device control request dispatcher.
  std::function<bool(const CmdRequest& request, uint32_t deadline,
                     std::vector<uint8_t> data, AsyncTransferReadyCB callback)>
      handle_control_transfer;

  // Device  data request dispatcher.
  std::function<bool(uint8_t endpoint, bool is_host_to_device,
                     uint32_t deadline, std::vector<uint8_t> data,
                     AsyncTransferReadyCB callback)>
      handle_data_transfer;
};

}  // namespace usbip
}  // namespace vadb

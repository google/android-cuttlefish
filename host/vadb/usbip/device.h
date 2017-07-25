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

#include "host/vadb/usbip/messages.h"

namespace vadb {
namespace usbip {

// The device descriptor of a USB device represents a USB device that is
// available for import.
class Device {
 public:
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

  // Device request dispatcher.
  std::function<bool(const CmdRequest& request,
                     const std::vector<uint8_t>& data_in,
                     std::vector<uint8_t>* data_out)>
      handle_request;
};

}  // namespace usbip
}  // namespace vadb

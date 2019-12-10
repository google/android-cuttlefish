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

#include <map>
#include <string>

#include "host/commands/virtual_usb_manager/usbip/device.h"

namespace vadb {
namespace usbip {
// Container for all virtual USB/IP devices.
// Stores devices by virtual BUS ID.
class DevicePool {
 public:
  // BusDevNumber is a pair uniquely identifying bus and device.
  struct BusDevNumber {
    uint16_t bus_number;
    uint16_t dev_number;

    bool operator<(BusDevNumber other) const {
      return (bus_number << 16 | dev_number) <
             (other.bus_number << 16 | other.dev_number);
    }
  };

  // Internal container type.
  using MapType = std::map<BusDevNumber, std::unique_ptr<Device>>;

  DevicePool() = default;
  virtual ~DevicePool() = default;

  // Add new device associated with virtual BUS ID.
  void AddDevice(BusDevNumber bus_id, std::unique_ptr<Device> device);

  // Get device associated with supplied virtual bus/device number.
  Device* GetDevice(BusDevNumber bus_dev_num) const;

  // Get total number of USB/IP devices.
  size_t Size() const { return devices_.size(); }

  MapType::const_iterator begin() const { return devices_.cbegin(); }
  MapType::const_iterator end() const { return devices_.cend(); }

 private:
  MapType devices_;

  DevicePool(const DevicePool&) = delete;
  DevicePool& operator=(const DevicePool&) = delete;
};

}  // namespace usbip
}  // namespace vadb

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

#include "host/vadb/usbip/device_pool.h"

#include <glog/logging.h>

namespace vadb {
namespace usbip {

void DevicePool::AddDevice(BusDevNumber bdn, std::unique_ptr<Device> device) {
  LOG_IF(FATAL, devices_.find(bdn) != devices_.end())
      << "Node already defined for bus=" << bdn.bus_number
      << ", dev=" << bdn.dev_number;
  devices_[bdn] = std::move(device);
}

Device* DevicePool::GetDevice(BusDevNumber bus_id) const {
  auto iter = devices_.find(bus_id);
  if (iter == devices_.end()) return nullptr;
  return iter->second.get();
}

}  // namespace usbip
}  // namespace vadb

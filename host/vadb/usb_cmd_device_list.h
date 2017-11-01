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

#include <functional>
#include <vector>
#include "common/libs/usbforward/protocol.h"
#include "host/vadb/usb_cmd.h"

namespace vadb {
// Request device list from remote host.
class USBCmdDeviceList : public USBCommand {
 public:
  // DeviceDiscoveredCB is a callback function invoked for every new discovered
  // device.
  using DeviceDiscoveredCB =
      std::function<void(const usb_forward::DeviceInfo&,
                         const std::vector<usb_forward::InterfaceInfo>&)>;

  USBCmdDeviceList(DeviceDiscoveredCB cb)
      : on_device_discovered_(std::move(cb)) {}

  ~USBCmdDeviceList() override = default;

  // Return usbforward command this instance is executing.
  usb_forward::Command Command() override { return usb_forward::CmdDeviceList; }

  // Send request body to the server.
  // Return false, if communication failed.
  bool OnRequest(const avd::SharedFD& data) override;

  // Receive response data from the server.
  // Return false, if communication failed.
  bool OnResponse(bool is_success, const avd::SharedFD& data) override;

 private:
  DeviceDiscoveredCB on_device_discovered_;
  USBCmdDeviceList(const USBCmdDeviceList& other) = delete;
  USBCmdDeviceList& operator=(const USBCmdDeviceList& other) = delete;
};
}  // namespace vadb

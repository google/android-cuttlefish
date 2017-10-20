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

#include "host/libs/vadb/usb_cmd.h"

namespace vadb {
// Request remote device attach (~open).
class USBCmdAttach : public USBCommand {
 public:
  USBCmdAttach(uint8_t bus_id, uint8_t dev_id);
  ~USBCmdAttach() override = default;

  // Return usbforward command this instance is executing.
  usb_forward::Command Command() override { return usb_forward::CmdAttach; }

  // Send request body to the server.
  // Return false, if communication failed.
  bool OnRequest(const avd::SharedFD& data) override;

  // Receive response data from the server.
  // Return false, if communication failed.
  bool OnResponse(bool is_success, const avd::SharedFD& data) override;

 private:
  usb_forward::AttachRequest req_;

  USBCmdAttach(const USBCmdAttach& other) = delete;
  USBCmdAttach& operator=(const USBCmdAttach& other) = delete;
};
}  // namespace vadb

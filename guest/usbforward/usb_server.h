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

#define LOG_TAG "UsbForward"

#include <map>
#include <string>
#include <libusb/libusb.h>

#include "common/libs/fs/shared_fd.h"

// USBServer exposes access to USB devices over pipe (virtio channel etc).
// Usage:
//
//     avd::SharedFD pipe = avd::SharedFD::Open(pipe_path, O_RDWR);
//     USBServer server(pipe);
//     CHECK(server.Init());
//     server.Serve();
class USBServer final {
 public:
  USBServer(const avd::SharedFD& fd) : fd_{fd} {}
  ~USBServer();

  // Populate list of USB devices.
  bool Init();

  // Serve incoming USB requests.
  void Serve();

 private:
  // Handle CmdDeviceList request.
  void HandleDeviceList();

  // Handle CmdAttach request.
  void HandleAttach();

  // Handle CmdExecute request.
  void HandleExecute();

  avd::SharedFD fd_;
  std::map<uint16_t, libusb_device*> devices_;
  std::map<uint16_t, libusb_device_handle*> attached_devices_;

  USBServer(const USBServer& other) = delete;
  USBServer& operator=(const USBServer& other) = delete;
};

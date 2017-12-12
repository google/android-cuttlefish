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

#include <list>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/usbip/device_pool.h"
#include "host/libs/vadb/virtual_adb_client.h"

namespace vadb {
// VirtualADBServer manages incoming VirtualUSB/ADB connections from QEmu.
class VirtualADBServer {
 public:
  VirtualADBServer(const std::string& usb_socket_name,
                   const std::string& usbip_socket_name)
      : name_(usb_socket_name), usbip_name_(usbip_socket_name) {}

  ~VirtualADBServer() = default;

  // Initialize this instance of Server.
  // Returns true, if initialization was successful.
  bool Init();

  // Pool of USB devices available to export.
  const usbip::DevicePool& Pool() const { return pool_; };

  // BeforeSelect is Called right before Select() to populate interesting
  // SharedFDs.
  void BeforeSelect(cvd::SharedFDSet* fd_read) const;

  // AfterSelect is Called right after Select() to detect and respond to changes
  // on affected SharedFDs.
  void AfterSelect(const cvd::SharedFDSet& fd_read);

 private:
  void HandleIncomingConnection();

  usbip::DevicePool pool_;
  std::string name_;
  std::string usbip_name_;
  cvd::SharedFD server_;
  std::list<VirtualADBClient> clients_;

  VirtualADBServer(const VirtualADBServer&) = delete;
  VirtualADBServer& operator=(const VirtualADBServer&) = delete;
};

}  // namespace vadb

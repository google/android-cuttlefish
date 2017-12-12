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
#include "host/libs/usbip/client.h"
#include "host/libs/usbip/device_pool.h"

namespace vadb {
namespace usbip {

class Server final {
 public:
  Server(const std::string& name, const DevicePool& device_pool);
  ~Server() = default;

  // Initialize this instance of Server.
  // Returns true, if initialization was successful.
  bool Init();

  // BeforeSelect is Called right before Select() to populate interesting
  // SharedFDs.
  void BeforeSelect(cvd::SharedFDSet* fd_read) const;

  // AfterSelect is Called right after Select() to detect and respond to changes
  // on affected SharedFDs.
  void AfterSelect(const cvd::SharedFDSet& fd_read);

 private:
  // Create USBIP server socket.
  // Returns true, if socket was successfully created.
  bool CreateServerSocket();

  // Handle new client connection.
  // New clients will be appended to clients_ list.
  void HandleIncomingConnection();

  std::string name_;
  cvd::SharedFD server_;
  std::list<Client> clients_;

  const DevicePool& device_pool_;

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
};

}  // namespace usbip
}  // namespace vadb

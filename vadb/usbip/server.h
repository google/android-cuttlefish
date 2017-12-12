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

#include "common/libs/fs/shared_fd.h"
#include "host/vadb/usbip/device_pool.h"
#include "host/vadb/usbip/client.h"

namespace vadb {
namespace usbip {

class Server final {
 public:
  Server(const DevicePool& device_pool);
  ~Server() = default;

  // Initialize this instance of Server.
  // Returns true, if initialization was successful.
  bool Init();

  // Main server loop. Handles all incoming connections as well as client data
  // exchange.
  void Serve();

  // StartAttachedByDefault tells clients to skip introduction and query phase
  // and go directly to command execution phase. This is particularly useful if
  // we want to make the stack automatic.
  void SetClientsAttachedByDefault(bool is_attached) {
    init_attached_state_ = is_attached;
  }

 private:
  // Create USBIP server socket.
  // Returns true, if socket was successfully created.
  bool CreateServerSocket();

  // Handle new client connection.
  // New clients will be appended to clients_ list.
  void HandleIncomingConnection();

  avd::SharedFD server_;
  std::list<Client> clients_;
  const DevicePool& device_pool_;
  bool init_attached_state_ = false;

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
};

}  // namespace usbip
}  // namespace vadb

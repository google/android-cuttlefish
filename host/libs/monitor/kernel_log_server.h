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

#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
//#include "host/libs/monitor/kernel_log_client.h"

namespace monitor {
// KernelLogServer manages incoming kernel log connection from QEmu. Only accept
// one connection.
class KernelLogServer {
 public:
  KernelLogServer(const std::string& socket_name,
                  const std::string& log_name,
                  bool deprecated_boot_completed);

  ~KernelLogServer() = default;

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
  // Create kernel log server socket.
  // Returns true, if socket was successfully created.
  bool CreateServerSocket();

  // Handle new client connection. Only accept one connection.
  void HandleIncomingConnection();

  // Respond to message from remote client.
  // Returns false, if client disconnected.
  bool HandleIncomingMessage();

  std::string name_;
  cvd::SharedFD server_fd_;
  cvd::SharedFD client_fd_;
  cvd::SharedFD log_fd_;
  std::string line_;
  bool deprecated_boot_completed_;

  KernelLogServer(const KernelLogServer&) = delete;
  KernelLogServer& operator=(const KernelLogServer&) = delete;
};

}  // namespace monitor

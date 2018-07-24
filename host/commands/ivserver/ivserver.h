#pragma once
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

#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/commands/ivserver/options.h"
#include "host/commands/ivserver/vsocsharedmem.h"

namespace ivserver {

// This class is responsible for orchestrating the setup and then serving
// new connections.
class IVServer final {
 public:
  // The qemu_channel_fd and client_channel_fd are the server sockets. If
  // non-positive values are provided the server will create those sockets
  // itself.
  IVServer(const IVServerOptions &options, int qemu_channel_fd,
           int client_channel_fd);
  IVServer(const IVServer &) = delete;
  IVServer& operator=(const IVServer&) = delete;

  // Serves incoming client and qemu connection.
  // This method should never return.
  void Serve();

 private:
  void HandleNewClientConnection();
  void HandleNewQemuConnection();

  std::unique_ptr<VSoCSharedMemory> vsoc_shmem_;
  cvd::SharedFD qemu_channel_;
  cvd::SharedFD client_channel_;
};

}  // namespace ivserver

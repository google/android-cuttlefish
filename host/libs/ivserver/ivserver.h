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

#include <json/json.h>
#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/ivserver/options.h"
#include "host/libs/ivserver/vsocsharedmem.h"

namespace ivserver {

// This class is responsible for orchestrating the setup and then serving
// new connections.
class IVServer final {
 public:
  IVServer(const IVServerOptions &options, const Json::Value &json_root);
  IVServer(const IVServer &) = delete;

  // Serves incoming client and qemu connection.
  // This method should never return.
  void Serve();

 private:
  void HandleNewClientConnection();
  void HandleNewQemuConnection();

  const Json::Value &json_root_;
  std::unique_ptr<VSoCSharedMemory> vsoc_shmem_;
  cvd::SharedFD qemu_channel_;
  cvd::SharedFD client_channel_;
};

}  // namespace ivserver

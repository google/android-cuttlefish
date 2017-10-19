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

#include <inttypes.h>
#include <unistd.h>
#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/ivserver/vsocsharedmem.h"

namespace ivserver {

// Handles a HAL deamon client connection & handshake.
class HaldClient final {
 public:
  static std::unique_ptr<HaldClient> New(const VSoCSharedMemory &shared_mem,
                                         const avd::SharedFD &client_fd);

 private:
  avd::SharedFD client_socket_;

  // Initialize new instance of HAL Client.
  HaldClient(const avd::SharedFD &client_fd);

  // Perform handshake with HAL Client.
  // If the region is not found approprate status (-1) is sent.
  // Note that for every new client connected a unique ClientConnection object
  // will be created and after the handshake it will be destroyed.
  bool PerformHandshake(const VSoCSharedMemory &shared_mem);

  HaldClient(const HaldClient &) = delete;
  HaldClient &operator=(const HaldClient &) = delete;
};

}  // namespace ivserver

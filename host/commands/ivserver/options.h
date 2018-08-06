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
#include <iostream>
#include <string>

namespace ivserver {

const uint16_t kIVServerMajorVersion = 1;
const uint16_t kIVServerMinorVersion = 0;
const uint32_t kIVServerDefaultShmSizeInMiB = 4;

//
// structure that contains the various options to start the server.
//
struct IVServerOptions final {
  IVServerOptions(const std::string &shm_file_path,
                  const std::string &qemu_socket_path,
                  const std::string &client_socket_path);

  //
  // We still need a friend here
  //
  friend std::ostream &operator<<(std::ostream &out,
                                  const IVServerOptions &opts);

  const std::string shm_file_path;
  const std::string qemu_socket_path;
  const std::string client_socket_path;
};

}  // namespace ivserver

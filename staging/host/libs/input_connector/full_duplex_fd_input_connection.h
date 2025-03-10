/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "host/libs/input_connector/input_connection.h"

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

// Connection to an input device over a full duplex file descriptor
// (socket pair, connection accepted on a socket, etc).
class FullDuplexFdInputConnection : public InputConnection {
 public:
  FullDuplexFdInputConnection(SharedFD conn);

  Result<void> WriteEvents(const void* data, size_t len) override;

 private:
  SharedFD conn_;
};

}  // namespace cuttlefish


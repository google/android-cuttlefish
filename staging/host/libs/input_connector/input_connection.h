/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

// A connection to a vhost user input device, allowing to inject events to the
// device.
class InputConnection {
 public:
  InputConnection(SharedFD conn);
  ~InputConnection() = default;

  Result<void> WriteEvents(const void* data, size_t len);

 private:
  SharedFD conn_;
};

}  // namespace cuttlefish

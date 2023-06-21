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

#include <cstdint>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace process_monitor_impl {

enum class ParentToChildMessageType : std::uint8_t {
  kStop = 1,
  kHostResume = 2,
  kHostSuspend = 3,
  kError = 4,
};

class ParentToChildMessage {
 public:
  ParentToChildMessage(const ParentToChildMessageType type);
  Result<void> Write(const SharedFD& fd);
  static Result<ParentToChildMessage> Read(const SharedFD& fd);
  bool Stop() const { return type_ == ParentToChildMessageType::kStop; }
  auto Type() const { return type_; }

 private:
  ParentToChildMessageType type_;
};

}  // namespace process_monitor_impl
}  // namespace cuttlefish

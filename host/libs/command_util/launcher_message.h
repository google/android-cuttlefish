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
#include <string>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/runner_defs.h"

namespace cuttlefish {
namespace run_cvd_msg_impl {

class LauncherActionMessage {
 public:
  /**
   * supported for backward compatibility, so only the following are accepted:
   *  kPowerwash, kRestart, kStatus, kStop
   */
  static Result<LauncherActionMessage> Create(const LauncherAction action);
  // use std::move(serialized_data) to avoid copy the buffer
  static Result<LauncherActionMessage> Create(const LauncherAction action,
                                              const ExtendedActionType type,
                                              std::string serialized_data);
  Result<void> WriteToFd(const SharedFD& fd);

 private:
  LauncherActionMessage(const LauncherAction action,
                        const ExtendedActionType type,
                        std::string serialized_data);
  // returns true if the action does not need extended field
  static bool IsShortAction(const LauncherAction action);
  static bool IsSupportedType(const ExtendedActionType type);

  const LauncherAction action_;
  const ExtendedActionType type_;
  // mostly for protobuf message
  const std::string serialized_data_;
};

}  // namespace run_cvd_msg_impl
}  // namespace cuttlefish

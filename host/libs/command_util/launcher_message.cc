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

#include "host/libs/command_util/launcher_message.h"

#include <cstdint>
#include <set>
#include <string>

#include <fmt/core.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace run_cvd_msg_impl {

Result<LauncherActionMessage> LauncherActionMessage::Create(
    const LauncherAction action) {
  CF_EXPECTF(IsShortAction(action), "LauncherAction ",
             static_cast<const char>(action),
             " is not supported by legacy LauncherActionMessage::Create()");
  auto action_message = CF_EXPECT(
      LauncherActionMessage::Create(action, ExtendedActionType::kUnused, ""));
  return action_message;
}

Result<LauncherActionMessage> LauncherActionMessage::Create(
    const LauncherAction action, const ExtendedActionType type,
    std::string serialized_data) {
  if (IsShortAction(action)) {
    const char action_char = static_cast<const char>(action);
    CF_EXPECTF(type == ExtendedActionType::kUnused, "The type of action \"",
               action_char, "\" should be ExtendedActionType::kUnused");
    if (!serialized_data.empty()) {
      LOG(DEBUG) << "serialized_data is ignored when the action is \""
                 << action_char << "\"";
    }
  }
  LauncherActionMessage new_message(action, type, std::move(serialized_data));
  /* To avoid -Wunused-private-field compilation error
   *
   * will be moved above when type_ is actually used.
   */
  CF_EXPECTF(IsSupportedType(new_message.type_), "ExtendedActionType ",
             static_cast<std::uint32_t>(new_message.type_),
             " is not supported.");
  return new_message;
}

LauncherActionMessage::LauncherActionMessage(const LauncherAction action,
                                             const ExtendedActionType type,
                                             std::string serialized_data)
    : action_(action),
      type_(type),
      serialized_data_(std::move(serialized_data)) {}

bool LauncherActionMessage::IsShortAction(const LauncherAction action) {
  std::set<LauncherAction> supported_actions{
      LauncherAction::kPowerwash,
      LauncherAction::kRestart,
      LauncherAction::kStatus,
      LauncherAction::kStop,
  };
  return Contains(supported_actions, action);
}

bool LauncherActionMessage::IsSupportedType(const ExtendedActionType type) {
  std::set<ExtendedActionType> supported_action_types{
      ExtendedActionType::kUnused,
      ExtendedActionType::kSuspend,
  };
  return Contains(supported_action_types, type);
}

Result<void> LauncherActionMessage::WriteToFd(const SharedFD& fd) {
  ssize_t bytes_sent = WriteAllBinary(fd, &action_);
  CF_EXPECTF(bytes_sent > 0,
             "Error sending launcher monitor the command: ", fd->StrError());
  CF_EXPECTF(bytes_sent == sizeof(action_),
             "LauncherActionMessage::WriteToFd() ",
             "did not send correct number of bytes");
  if (IsShortAction(action_)) {
    return {};
  }
  return CF_ERR(fmt::format("Action \"{}\" is not yet supported",
                            static_cast<const char>(action_)));
}

}  // namespace run_cvd_msg_impl
}  // namespace cuttlefish

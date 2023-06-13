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
      ExtendedActionType::kResume,
  };
  return Contains(supported_action_types, type);
}

static Result<void> WriteBuffer(const SharedFD& fd, const std::string& buf,
                                const std::string& description) {
  CF_EXPECT(fd->IsOpen(), "The file descriptor to write is not open.");
  ssize_t bytes_sent = WriteAll(fd, buf);
  CF_EXPECTF(bytes_sent > 0, "Error sending ", description,
             " to launcher monitor: ", fd->StrError());
  CF_EXPECTF(bytes_sent == buf.size(), "LauncherActionMessage::WriteToFd() ",
             "did not send correct number of bytes");
  return {};
}

// use this when sizeof(T) is the number of bytes we want to send
template <typename T>
static Result<void> WriteSizeOfT(const SharedFD& fd, const T& t,
                                 const std::string& description) {
  CF_EXPECT(fd->IsOpen(), "The file descriptor to write is not open.");
  const char* start_addr = reinterpret_cast<const char*>(&t);
  std::vector<char> tmp{start_addr, start_addr + sizeof(T)};
  std::string buf{tmp.begin(), tmp.end()};
  CF_EXPECT(WriteBuffer(fd, buf, description));
  return {};
}

Result<void> LauncherActionMessage::WriteToFd(const SharedFD& fd) {
  CF_EXPECT(WriteSizeOfT(fd, action_, "LauncherAction"));
  if (IsShortAction(action_)) {
    return {};
  }
  CF_EXPECT(WriteSizeOfT(fd, type_, "ExtendedActionType"));
  const SerializedDataSizeType length = serialized_data_.size();
  CF_EXPECT(WriteSizeOfT(fd, length, "Length of serialized data"));
  if (!serialized_data_.empty()) {
    CF_EXPECT(WriteBuffer(fd, serialized_data_, "serialized data"));
  }
  return {};
}

Result<LauncherActionMessage> LauncherActionMessage::ReadFromFd(
    const SharedFD& fd) {
  CF_EXPECT(fd->IsOpen(), "The file descriptor for ReadFromFd is not open.");
  LauncherAction action;
  ssize_t n_bytes = 0;
  CF_EXPECTF((n_bytes = fd->Read(&action, sizeof(action))) > 0,
             "The returned n_bytes is not ", std::to_string(sizeof(action)),
             " but ", std::to_string(n_bytes));
  if (IsShortAction(action)) {
    return CF_EXPECT(LauncherActionMessage::Create(action));
  }
  ExtendedActionType type;
  CF_EXPECTF((n_bytes = fd->Read(&type, sizeof(type))) > 0,
             "The returned n_bytes is not ", std::to_string(sizeof(type)),
             " but ", std::to_string(n_bytes));
  SerializedDataSizeType length = 0;
  CF_EXPECTF((n_bytes = fd->Read(&length, sizeof(length))) > 0,
             "The returned n_bytes is not ", std::to_string(sizeof(length)),
             " but ", std::to_string(n_bytes));
  if (length == 0) {
    return CF_EXPECT(LauncherActionMessage::Create(action, type, ""));
  }
  std::string serialized_data(length, 0);
  CF_EXPECTF((n_bytes = ReadExact(fd, &serialized_data)) > 0,
             "The returned n_bytes is not ", std::to_string(sizeof(length)),
             " but ", std::to_string(n_bytes));
  auto message =
      CF_EXPECT(LauncherActionMessage::Create(action, type, serialized_data));
  return message;
}

LauncherAction LauncherActionMessage::Action() const { return action_; }
ExtendedActionType LauncherActionMessage::Type() const { return type_; }
const std::string& LauncherActionMessage::SerializedData() const {
  return serialized_data_;
}

}  // namespace run_cvd_msg_impl
}  // namespace cuttlefish

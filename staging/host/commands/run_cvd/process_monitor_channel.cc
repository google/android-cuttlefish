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

#include "host/commands/run_cvd/process_monitor_channel.h"

#include <string>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace process_monitor_impl {

ParentToChildMessage::ParentToChildMessage(const ParentToChildMessageType type)
    : type_(type) {}

Result<void> ParentToChildMessage::Write(const SharedFD& fd) {
  CF_EXPECTF(fd->IsOpen(), "File descriptor to write ParentToChildMessage",
             " is closed.");
  const auto n_bytes = WriteAllBinary(fd, &type_);
  std::string err_msg("Failed to communicate with monitor socket");
  CF_EXPECTF(n_bytes == sizeof(type_),
             "{} : {}. Expected to write {} bytes but wrote {} bytes.", err_msg,
             fd->StrError(), sizeof(type_), n_bytes);
  return {};
}

Result<ParentToChildMessage> ParentToChildMessage::Read(const SharedFD& fd) {
  ParentToChildMessageType type = ParentToChildMessageType::kError;
  CF_EXPECTF(fd->IsOpen(), "File descriptor to read ParentToChildMessage",
             "from is closed.");
  std::string err_msg("Could not read message from parent");
  const auto n_bytes = ReadExactBinary(fd, &type);
  CF_EXPECTF(n_bytes == sizeof(type),
             "{} : {}. Expected To read {} bytes but actually read {} bytes",
             err_msg, fd->StrError(), sizeof(type), n_bytes);
  return ParentToChildMessage{type};
}

ChildToParentResponse::ChildToParentResponse(
    const ChildToParentResponseType type)
    : type_(type) {}

Result<void> ChildToParentResponse::Write(const SharedFD& fd) {
  CF_EXPECTF(fd->IsOpen(), "File descriptor to write ChildToParentResponse",
             " is closed.");
  const auto n_bytes = WriteAllBinary(fd, &type_);
  std::string err_msg("Failed to communicate with monitor socket");
  CF_EXPECTF(n_bytes == sizeof(type_),
             "{} : {}. Expected to write {} bytes but wrote {} bytes.", err_msg,
             fd->StrError(), sizeof(type_), n_bytes);
  return {};
}

Result<ChildToParentResponse> ChildToParentResponse::Read(const SharedFD& fd) {
  ChildToParentResponseType type = ChildToParentResponseType::kFailure;
  CF_EXPECTF(fd->IsOpen(), "File descriptor to read ChildToParentResponse",
             "from is closed.");
  std::string err_msg("Could not read response from parent");
  const auto n_bytes = ReadExactBinary(fd, &type);
  CF_EXPECTF(n_bytes == sizeof(type),
             "{} : {}. Expected To read {} bytes but actually read {} bytes",
             err_msg, fd->StrError(), sizeof(type), n_bytes);
  return ChildToParentResponse{type};
}

}  // namespace process_monitor_impl
}  // namespace cuttlefish

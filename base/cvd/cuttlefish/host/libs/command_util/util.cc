/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/libs/command_util/util.h"

#include "sys/types.h"

#include <optional>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/result.h"
#include "host/libs/command_util/runner/defs.h"

namespace cuttlefish {
namespace {

bool IsShortAction(const LauncherAction action) {
  switch (action) {
    case LauncherAction::kFail:
    case LauncherAction::kPowerwash:
    case LauncherAction::kRestart:
    case LauncherAction::kStatus:
    case LauncherAction::kStop:
      return true;
    default:
      return false;
  };
}

template <typename T>
static Result<void> WriteAllBinaryResult(const SharedFD& fd, const T* t) {
  ssize_t n = WriteAllBinary(fd, t);
  CF_EXPECTF(n > 0, "Write error: {}", fd->StrError());
  CF_EXPECT(n == sizeof(*t), "Unexpected EOF on write");
  return {};
}

// Rerturns true if something was read, false if the file descriptor reached
// EOF.
template <typename T>
static Result<bool> ReadExactBinaryResult(const SharedFD& fd, T* t) {
  ssize_t n = ReadExactBinary(fd, t);
  if (n == 0) {
    return false;
  }
  CF_EXPECTF(n > 0, "Read error: {}", fd->StrError());
  CF_EXPECT(n == sizeof(*t), "Unexpected EOF on read");
  return true;
}

}  // namespace

Result<RunnerExitCodes> ReadExitCode(SharedFD monitor_socket) {
  RunnerExitCodes exit_codes;
  CF_EXPECT(ReadExactBinaryResult(monitor_socket, &exit_codes),
            "Error reading RunnerExitCodes");
  return exit_codes;
}

Result<void> WaitForRead(SharedFD monitor_socket, const int timeout_seconds) {
  // Perform a select with a timeout to guard against launcher hanging
  SharedFDSet read_set;
  read_set.Set(monitor_socket);
  struct timeval timeout = {timeout_seconds, 0};
  int select_result = Select(&read_set, nullptr, nullptr,
                             timeout_seconds <= 0 ? nullptr : &timeout);
  CF_EXPECT(select_result != 0,
            "Timeout expired waiting for launcher monitor to respond");
  CF_EXPECT(
      select_result > 0,
      "Failed communication with the launcher monitor: " << strerror(errno));
  return {};
}

Result<void> RunLauncherAction(SharedFD monitor_socket, LauncherAction action,
                               std::optional<int> timeout_seconds) {
  CF_EXPECTF(IsShortAction(action),
             "PerformActionRequest doesn't support extended action \"{}\"",
             static_cast<const char>(action));
  CF_EXPECT(WriteAllBinaryResult(monitor_socket, &action),
            "Error writing LauncherAction");

  if (timeout_seconds.has_value()) {
    CF_EXPECT(WaitForRead(monitor_socket, timeout_seconds.value()));
  }
  LauncherResponse response;
  CF_EXPECT(ReadExactBinaryResult(monitor_socket, &response),
            "Error reading LauncherResponse");
  CF_EXPECT_EQ((int)response, (int)LauncherResponse::kSuccess);
  return {};
}

}  // namespace cuttlefish

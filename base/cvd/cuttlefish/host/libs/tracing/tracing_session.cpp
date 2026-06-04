/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/tracing/tracing_session.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/wait_for_file.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/tracing/tracing_utils.h"

namespace cuttlefish {

Result<std::unique_ptr<TracingSession>> TracingSession::StartBlocking(
    absl::Duration timeout) {
  auto existing_socket_path_opt = StringFromEnv(kTracingSocketPathEnv);
  CF_EXPECT(!existing_socket_path_opt,
            "An existing tracing session is already active.");

  const std::string socket_path =
      absl::StrCat(TempDir(), "/cf_tracing_", GetProcessId(), ".sock");

  int res = setenv(kTracingSocketPathEnv, socket_path.c_str(), 1);
  CF_EXPECT_EQ(res, 0, "Failed to set tracing environment variable");

  Subprocess server_subprocess =
      Command(HostBinaryPath("tracing_forwarder")).Start();
  CF_EXPECT(server_subprocess.Started(), "Failed to start tracing_forwarder.");

  CF_EXPECT(WaitForFile(socket_path, absl::ToInt64Seconds(timeout)),
            "Timed out after "
                << absl::FormatDuration(timeout)
                << " waiting for the tracing forwarder to start.");

  return std::unique_ptr<TracingSession>(
      new TracingSession(socket_path, std::move(server_subprocess)));
}

TracingSession::TracingSession(std::string socket_path, Subprocess server)
    : socket_path_(socket_path), server_subprocess_(std::move(server)) {}

TracingSession::~TracingSession() {
  bool need_stop = false;
  if (server_subprocess_.Started()) {
    if (server_subprocess_.SendSignal(SIGTERM).ok()) {
      siginfo_t infop;
      int result = server_subprocess_.Wait(&infop, WEXITED);
      if (result != 0) {
        LOG(ERROR) << "Failed to wait for tracing to stop.";
        need_stop = true;
      }
    } else {
      LOG(ERROR) << "Failed to request tracing to stop.";
      need_stop = true;
    }
  }
  if (need_stop) {
    StopperResult stop_result = server_subprocess_.Stop();
    if (stop_result != StopperResult::kStopSuccess) {
      LOG(ERROR) << "Failed to fully stop tracing.";
    }
  }

  unlink(socket_path_.c_str());
  unsetenv(kTracingSocketPathEnv);
}

}  // namespace cuttlefish

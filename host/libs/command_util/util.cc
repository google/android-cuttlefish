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

#include "sys/time.h"
#include "sys/types.h"

#include <string>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/command_util/launcher_message.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

template <typename T>
Result<T> ReadFromMonitor(const SharedFD& monitor_socket) {
  T response;
  ssize_t bytes_recv = ReadExactBinary(monitor_socket, &response);
  CF_EXPECT(bytes_recv != 0, "Launcher socket closed unexpectedly");
  CF_EXPECT(bytes_recv > 0, "Error receiving response from launcher monitor: "
                                << monitor_socket->StrError());
  CF_EXPECT(bytes_recv == sizeof(response),
            "Launcher response not correct number of bytes");
  return response;
}

}  // namespace

Result<LauncherResponse> ReadLauncherResponse(const SharedFD& monitor_socket) {
  return ReadFromMonitor<LauncherResponse>(monitor_socket);
}

Result<RunnerExitCodes> ReadExitCode(const SharedFD& monitor_socket) {
  return ReadFromMonitor<RunnerExitCodes>(monitor_socket);
}

Result<SharedFD> GetLauncherMonitorFromInstance(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    const int timeout_seconds) {
  std::string monitor_path = instance_config.launcher_monitor_socket_path();
  CF_EXPECT(!monitor_path.empty(), "No path to launcher monitor found");

  SharedFD monitor_socket = SharedFD::SocketLocalClient(
      monitor_path.c_str(), false, SOCK_STREAM, timeout_seconds);
  CF_EXPECT(monitor_socket->IsOpen(),
            "Unable to connect to launcher monitor at "
                << monitor_path << ":" << monitor_socket->StrError());
  return monitor_socket;
}

Result<SharedFD> GetLauncherMonitor(const CuttlefishConfig& config,
                                    const int instance_num,
                                    const int timeout_seconds) {
  auto instance_config = config.ForInstance(instance_num);
  return GetLauncherMonitorFromInstance(instance_config, timeout_seconds);
}

Result<void> WriteLauncherAction(const SharedFD& monitor_socket,
                                 const LauncherAction request) {
  CF_EXPECT(WriteLauncherActionWithData(monitor_socket, request,
                                        ExtendedActionType::kUnused, ""));
  return {};
}

Result<void> WriteLauncherActionWithData(const SharedFD& monitor_socket,
                                         const LauncherAction request,
                                         const ExtendedActionType type,
                                         std::string serialized_data) {
  using run_cvd_msg_impl::LauncherActionMessage;
  auto message = CF_EXPECT(
      LauncherActionMessage::Create(request, type, std::move(serialized_data)));
  CF_EXPECT(message.WriteToFd(monitor_socket));
  return {};
}

Result<void> WaitForRead(const SharedFD& monitor_socket,
                         const int timeout_seconds) {
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

}  // namespace cuttlefish

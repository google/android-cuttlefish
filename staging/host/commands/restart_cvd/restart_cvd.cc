/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/environment.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_int32(instance_num, cuttlefish::GetInstance(),
             "Which instance to restart");

DEFINE_int32(wait_for_launcher, 30,
             "How many seconds to wait for the launcher to respond to the status "
             "command. A value of zero means wait indefinetly");

DEFINE_int32(boot_timeout, 1000, "How many seconds to wait for the device to "
                                 "reboot.");

namespace cuttlefish {
namespace {

int RestartCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return 1;
  }

  auto instance = config->ForInstance(FLAGS_instance_num);
  auto monitor_path = instance.launcher_monitor_socket_path();
  if (monitor_path.empty()) {
    LOG(ERROR) << "No path to launcher monitor found";
    return 2;
  }
  // This may hang if the server never picks up the connection.
  auto monitor_socket = SharedFD::SocketLocalClient(
      monitor_path.c_str(), false, SOCK_STREAM, FLAGS_wait_for_launcher);
  if (!monitor_socket->IsOpen()) {
    LOG(ERROR) << "Unable to connect to launcher monitor at " << monitor_path
               << ": " << monitor_socket->StrError();
    return 3;
  }
  auto request = LauncherAction::kRestart;
  auto bytes_sent = monitor_socket->Send(&request, sizeof(request), 0);
  if (bytes_sent < 0) {
    LOG(ERROR) << "Error sending launcher monitor the status command: "
               << monitor_socket->StrError();
    return 4;
  }
  // Perform a select with a timeout to guard against launcher hanging
  SharedFDSet read_set;
  read_set.Set(monitor_socket);
  struct timeval timeout = {FLAGS_wait_for_launcher, 0};
  int selected = Select(&read_set, nullptr, nullptr,
                        FLAGS_wait_for_launcher <= 0 ? nullptr : &timeout);
  if (selected < 0){
    LOG(ERROR) << "Failed communication with the launcher monitor: "
               << strerror(errno);
    return 5;
  }
  if (selected == 0) {
    LOG(ERROR) << "Timeout expired waiting for launcher monitor to respond";
    return 6;
  }
  LauncherResponse response;
  auto bytes_recv = monitor_socket->Recv(&response, sizeof(response), 0);
  if (bytes_recv < 0) {
    LOG(ERROR) << "Error receiving response from launcher monitor: "
               << monitor_socket->StrError();
    return 7;
  }
  LOG(INFO) << "Requesting restart";
  if (response != LauncherResponse::kSuccess) {
    LOG(ERROR) << "Received '" << static_cast<char>(response)
               << "' response from launcher monitor for restart request";
    return 8;
  }
  LOG(INFO) << "Waiting for device to boot up again";

  read_set.Set(monitor_socket);
  timeout = {FLAGS_boot_timeout, 0};
  selected = Select(&read_set, nullptr, nullptr,
                    FLAGS_boot_timeout <= 0 ? nullptr : &timeout);
  if (selected < 0){
    LOG(ERROR) << "Failed communication with the launcher monitor: "
               << strerror(errno);
    return 5;
  }
  if (selected == 0) {
    LOG(ERROR) << "Timeout expired waiting for launcher monitor to respond";
    return 6;
  }

  RunnerExitCodes exit_code;
  bytes_recv = ReadExactBinary(monitor_socket, &exit_code);
  if (bytes_recv < 0) {
    LOG(ERROR) << "Error in stream response: " << monitor_socket->StrError();
    return 9;
  } else if (bytes_recv == 0) {
    LOG(ERROR) << "Launcher socket closed unexpectedly";
    return 10;
  } else if (bytes_recv != sizeof(exit_code)) {
    LOG(ERROR) << "Launcher response was too short";
    return 11;
  } else if (exit_code == RunnerExitCodes::kVirtualDeviceBootFailed) {
    LOG(ERROR) << "Boot failed";
    return 12;
  } else if (exit_code != RunnerExitCodes::kSuccess) {
    LOG(ERROR) << "Unknown response: " << (int) exit_code;
    return 13;
  }
  LOG(INFO) << "Restart successful";
  return 0;
}

} // namespace
} // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::RestartCvdMain(argc, argv);
}

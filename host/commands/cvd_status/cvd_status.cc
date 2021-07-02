/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/tee_logging.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

int CvdStatusMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  ::android::base::SetLogger(LogToStderrAndFiles({}));

  std::vector<Flag> flags;

  std::int32_t wait_for_launcher;
  flags.emplace_back(
      GflagsCompatFlag("wait_for_launcher", wait_for_launcher)
          .Help("How many seconds to wait for the launcher to respond to the "
                "status command. A value of zero means wait indefinitely"));

  flags.emplace_back(HelpFlag(flags));
  flags.emplace_back(UnexpectedArgumentGuard());

  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CHECK(ParseFlags(flags, args)) << "Could not process command line flags.";

  auto config = CuttlefishConfig::Get();
  CHECK(config) << "Failed to obtain config object";

  auto instance = config->ForDefaultInstance();
  auto monitor_path = instance.launcher_monitor_socket_path();
  CHECK(!monitor_path.empty()) << "No path to launcher monitor found";

  auto monitor_socket = SharedFD::SocketLocalClient(
      monitor_path.c_str(), false, SOCK_STREAM, wait_for_launcher);
  CHECK(monitor_socket->IsOpen())
      << "Unable to connect to launcher monitor at " << monitor_path << ": "
      << monitor_socket->StrError();

  auto request = LauncherAction::kStatus;
  auto bytes_sent = monitor_socket->Send(&request, sizeof(request), 0);
  CHECK(bytes_sent > 0) << "Error sending launcher monitor the status command: "
                        << monitor_socket->StrError();

  // Perform a select with a timeout to guard against launcher hanging
  SharedFDSet read_set;
  read_set.Set(monitor_socket);
  struct timeval timeout = {wait_for_launcher, 0};
  int selected = Select(&read_set, nullptr, nullptr,
                        wait_for_launcher <= 0 ? nullptr : &timeout);
  CHECK(selected >= 0) << "Failed communication with the launcher monitor: "
                       << strerror(errno);
  CHECK(selected > 0)
      << "Timeout expired waiting for launcher monitor to respond";

  LauncherResponse response;
  auto bytes_recv = monitor_socket->Recv(&response, sizeof(response), 0);
  CHECK(bytes_recv > 0) << "Error receiving response from launcher monitor: "
                        << monitor_socket->StrError();
  CHECK(response == LauncherResponse::kSuccess)
      << "Received '" << static_cast<char>(response)
      << "' response from launcher monitor";

  LOG(INFO) << "run_cvd is active.";
  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::CvdStatusMain(argc, argv);
}

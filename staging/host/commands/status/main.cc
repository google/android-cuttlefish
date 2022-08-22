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

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
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

#define CHECK_PRINT(print, condition, message)                               \
  if (print) {                                                               \
    if (!(condition)) {                                                      \
      std::cout << "      Status: Stopped (" << message << ")" << std::endl; \
      exit(0);                                                               \
    }                                                                        \
  } else                                                                     \
    CHECK(condition) << message

namespace cuttlefish {

int CvdStatusMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  ::android::base::SetLogger(LogToStderrAndFiles({}));

  std::vector<Flag> flags;

  std::int32_t wait_for_launcher;
  Json::Value device_info;
  flags.emplace_back(
      GflagsCompatFlag("wait_for_launcher", wait_for_launcher)
          .Help("How many seconds to wait for the launcher to respond to the "
                "status command. A value of zero means wait indefinitely"));
  std::string instance_name;
  flags.emplace_back(GflagsCompatFlag("instance_name", instance_name)
                         .Help("Name of the instance to check. If not "
                               "provided, DefaultInstance is used."));
  bool print;
  flags.emplace_back(GflagsCompatFlag("print", print)
                         .Help("If provided, prints status and instance config "
                               "information to stdout instead of CHECK"));
  bool all_instances;
  flags.emplace_back(
      GflagsCompatFlag("all_instances", all_instances)
          .Help("List all instances status and instance config information."));

  flags.emplace_back(HelpFlag(flags));
  flags.emplace_back(UnexpectedArgumentGuard());

  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CHECK(ParseFlags(flags, args)) << "Could not process command line flags.";

  auto config = CuttlefishConfig::Get();
  CHECK(config) << "Failed to obtain config object";

  auto instance_names = all_instances ? config->instance_names()
                                      : std::vector<std::string>{instance_name};

  Json::Value devices_info;
  for (int index = 0; index < instance_names.size(); index++) {
    std::string instance_name = instance_names[index];
    auto instance = instance_name.empty()
                        ? config->ForDefaultInstance()
                        : config->ForInstanceName(instance_name);
    auto monitor_path = instance.launcher_monitor_socket_path();
    CHECK_PRINT(print, !monitor_path.empty(),
                "No path to launcher monitor found");

    auto monitor_socket = SharedFD::SocketLocalClient(
        monitor_path.c_str(), false, SOCK_STREAM, wait_for_launcher);
    CHECK_PRINT(print, monitor_socket->IsOpen(),
                "Unable to connect to launcher monitor at " + monitor_path +
                    ": " + monitor_socket->StrError());

    auto request = LauncherAction::kStatus;
    auto bytes_sent = monitor_socket->Send(&request, sizeof(request), 0);
    CHECK_PRINT(print, bytes_sent > 0,
                "Error sending launcher monitor the status command: " +
                    monitor_socket->StrError());

    // Perform a select with a timeout to guard against launcher hanging
    SharedFDSet read_set;
    read_set.Set(monitor_socket);
    struct timeval timeout = {wait_for_launcher, 0};
    int selected = Select(&read_set, nullptr, nullptr,
                          wait_for_launcher <= 0 ? nullptr : &timeout);
    CHECK_PRINT(
        print, selected >= 0,
        std::string("Failed communication with the launcher monitor: ") +
            strerror(errno));
    CHECK_PRINT(print, selected > 0,
                "Timeout expired waiting for launcher monitor to respond");

    LauncherResponse response;
    auto bytes_recv = monitor_socket->Recv(&response, sizeof(response), 0);
    CHECK_PRINT(
        print, bytes_recv > 0,
        std::string("Error receiving response from launcher monitor: ") +
            monitor_socket->StrError());
    CHECK_PRINT(print, response == LauncherResponse::kSuccess,
                std::string("Received '") + static_cast<char>(response) +
                    "' response from launcher monitor");
    if (print) {
      devices_info[index]["assembly_dir"] = config->assembly_dir();
      devices_info[index]["instance_name"] = instance.instance_name();
      devices_info[index]["instance_dir"] = instance.instance_dir();
      devices_info[index]["web_access"] =
          "https://" + config->sig_server_address() + ":" +
          std::to_string(config->sig_server_port()) +
          "/client.html?deviceId=" + instance.instance_name();
      devices_info[index]["adb_serial"] = instance.adb_ip_and_port();
      devices_info[index]["webrtc_port"] =
          std::to_string(config->sig_server_port());
      for (int i = 0; i < instance.display_configs().size(); i++) {
        devices_info[index]["displays"][i] =
            std::to_string(instance.display_configs()[i].width) + " x " +
            std::to_string(instance.display_configs()[i].height) + " ( " +
            std::to_string(instance.display_configs()[i].dpi) + " )";
      }
      devices_info[index]["status"] = "Running";
      if (index == (instance_names.size() - 1)) {
        std::cout << devices_info.toStyledString() << std::endl;
      }
    } else {
      LOG(INFO) << "run_cvd is active.";
    }
  }
  return 0;
}
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::CvdStatusMain(argc, argv);
}

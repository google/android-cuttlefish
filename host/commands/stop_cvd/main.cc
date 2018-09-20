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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/environment.h"
#include "host/commands/launch/launcher_defs.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"
#include "host/libs/vm_manager/libvirt_manager.h"

DEFINE_int32(wait_for_launcher, 5,
             "How many seconds to wait for the launcher to respond to the stop "
             "command. A value of zero means wait indefinetly");

namespace {
// Gets a set of the possible process groups of a previous launch
std::set<pid_t> GetCandidateProcessGroups() {
  std::string cmd = "fuser";
  // Add the instance directory for qemu
  cmd += " " + cvd::StringFromEnv("HOME", ".") + "/cuttlefish_runtime/*";
  // Add the instance directory for libvirt
  auto libvirt_instance_dir =
      std::string("/var/run/libvirt-") + vsoc::kDefaultUuidPrefix;
  cmd += " " + vsoc::GetPerInstanceDefault(libvirt_instance_dir.c_str()) + "/*";
  cmd += " " + vsoc::GetPerInstanceDefault("/dev/shm/cvd-");
  std::shared_ptr<FILE> cmd_out(popen(cmd.c_str(), "r"), pclose);
  if (!cmd_out) {
    LOG(ERROR) << "Unable to execute '" << cmd << "': " << strerror(errno);
    return {};
  }
  int64_t pid;
  std::set<pid_t> ret{};
  while(fscanf(cmd_out.get(), "%" PRId64, &pid) != EOF) {
    pid_t pgid = getpgid(static_cast<pid_t>(pid));
    if (pgid < 0) {
      LOG(ERROR) << "Unable to get process group of " << pid << ": "
                 << strerror(errno);
      continue;
    }
    ret.insert(pgid);
  }
  // The process group of stop_cvd should not be killed
  ret.erase(getpgrp());
  return ret;
}

int FallBackStop() {
  auto exit_code = 1; // Having to fallback is an error
  if (vm_manager::VmManager::IsVmManagerSupported(
          vm_manager::LibvirtManager::name())) {
    // Libvirt doesn't run as the same user as stop_cvd, so we must stop it
    // through the manager. Qemu on the other hand would get killed by the
    // commands below.
    vsoc::CuttlefishConfig config;
    config.set_instance_dir(std::string("/var/run/libvirt-") +
                            vsoc::kDefaultUuidPrefix);
    auto vm_manager =
      vm_manager::VmManager::Get(vm_manager::LibvirtManager::name(), &config);
    if (!vm_manager->Stop()) {
      LOG(WARNING) << "Failed to stop the libvirt domain: Is it still running? "
                      "Is it using qemu_cli?";
      exit_code |= 2;
    }
  }

  auto process_groups = GetCandidateProcessGroups();
  for (auto pgid: process_groups) {
    LOG(INFO) << "Sending SIGKILL to process group " << pgid;
    auto retval = killpg(pgid, SIGKILL);
    if (retval < 0) {
      LOG(ERROR) << "Failed to kill process group " << pgid << ": "
                 << strerror(errno);
      exit_code |= 4;
    }
  }

  return exit_code;
}
}  // anonymous namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = vsoc::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return FallBackStop();
  }

  auto monitor_path = config->launcher_monitor_socket_path();
  if (monitor_path.empty()) {
    LOG(ERROR) << "No path to launcher monitor found";
    return FallBackStop();
  }
  auto monitor_socket = cvd::SharedFD::SocketLocalClient(monitor_path.c_str(),
                                                         false, SOCK_STREAM);
  if (!monitor_socket->IsOpen()) {
    LOG(ERROR) << "Unable to connect to launcher monitor at " << monitor_path
               << ": " << monitor_socket->StrError();
    return FallBackStop();
  }
  auto request = cvd::LauncherAction::kStop;
  auto bytes_sent = monitor_socket->Send(&request, sizeof(request), 0);
  if (bytes_sent < 0) {
    LOG(ERROR) << "Error sending launcher monitor the stop command: "
               << monitor_socket->StrError();
    return FallBackStop();
  }
  // Perform a select with a timeout to guard against launcher hanging
  cvd::SharedFDSet read_set;
  read_set.Set(monitor_socket);
  struct timeval timeout = {FLAGS_wait_for_launcher, 0};
  int selected = cvd::Select(&read_set, nullptr, nullptr,
                             FLAGS_wait_for_launcher <= 0 ? nullptr : &timeout);
  if (selected < 0){
    LOG(ERROR) << "Failed communication with the launcher monitor: "
               << strerror(errno);
    return FallBackStop();
  }
  if (selected == 0) {
    LOG(ERROR) << "Timeout expired waiting for launcher monitor to respond";
    return FallBackStop();
  }
  cvd::LauncherResponse response;
  auto bytes_recv = monitor_socket->Recv(&response, sizeof(response), 0);
  if (bytes_recv < 0) {
    LOG(ERROR) << "Error receiving response from launcher monitor: "
               << monitor_socket->StrError();
    return FallBackStop();
  }
  if (response != cvd::LauncherResponse::kSuccess) {
    LOG(ERROR) << "Received '" << static_cast<char>(response)
               << "' response from launcher monitor";
    return FallBackStop();
  }
  LOG(INFO) << "Successfully stopped device";
  return 0;
}

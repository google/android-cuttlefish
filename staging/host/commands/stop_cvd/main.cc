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

#include <dirent.h>
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

#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/environment.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/allocd/request.h"
#include "host/libs/allocd/utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"

DEFINE_int32(wait_for_launcher, 5,
             "How many seconds to wait for the launcher to respond to the stop "
             "command. A value of zero means wait indefinetly");

namespace cuttlefish {
namespace {

std::set<std::string> FallbackPaths() {
  std::set<std::string> paths;
  std::string parent_path = StringFromEnv("HOME", ".");
  paths.insert(parent_path + "/cuttlefish_assembly");
  paths.insert(parent_path + "/cuttlefish_assembly/*");

  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(parent_path.c_str()), closedir);
  for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
    std::string subdir(entity->d_name);
    if (!android::base::StartsWith(subdir, "cuttlefish_runtime.")) {
      continue;
    }
    auto instance_dir = parent_path + "/" + subdir;
    // Add the instance directory
    paths.insert(instance_dir);
    // Add files in instance dir
    paths.insert(instance_dir + "/*");
    // Add files in the tombstone directory
    paths.insert(instance_dir + "/tombstones/*");
    // Add files in the internal directory
    paths.insert(instance_dir + "/" + std::string(kInternalDirName) + "/*");
    // Add files in the shared directory
    paths.insert(instance_dir + "/" + std::string(kSharedDirName) + "/*");
  }
  return paths;
}

std::set<std::string> PathsForInstance(const CuttlefishConfig& config,
                                       const CuttlefishConfig::InstanceSpecific instance) {
  return {
    config.assembly_dir(),
    config.assembly_dir() + "/*",
    instance.instance_dir(),
    instance.PerInstancePath("*"),
    instance.PerInstancePath("tombstones"),
    instance.PerInstancePath("tombstones/*"),
    instance.instance_internal_dir(),
    instance.PerInstanceInternalPath("*"),
    instance.PerInstancePath(kSharedDirName),
    instance.PerInstancePath(kSharedDirName) + "/*",
  };
}

// Gets a set of the possible process groups of a previous launch
std::set<pid_t> GetCandidateProcessGroups(const std::set<std::string>& paths) {
  std::stringstream cmd;
  cmd << "lsof -t 2>/dev/null";
  for (const auto& path : paths) {
    cmd << " " << path;
  }
  std::string cmd_str = cmd.str();
  std::shared_ptr<FILE> cmd_out(popen(cmd_str.c_str(), "r"), pclose);
  if (!cmd_out) {
    LOG(ERROR) << "Unable to execute '" << cmd_str << "': " << strerror(errno);
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

int FallBackStop(const std::set<std::string>& paths) {
  auto exit_code = 1; // Having to fallback is an error

  auto process_groups = GetCandidateProcessGroups(paths);
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

bool CleanStopInstance(const CuttlefishConfig::InstanceSpecific& instance) {
  auto monitor_path = instance.launcher_monitor_socket_path();
  if (monitor_path.empty()) {
    LOG(ERROR) << "No path to launcher monitor found";
    return false;
  }
  auto monitor_socket = SharedFD::SocketLocalClient(
      monitor_path.c_str(), false, SOCK_STREAM, FLAGS_wait_for_launcher);
  if (!monitor_socket->IsOpen()) {
    LOG(ERROR) << "Unable to connect to launcher monitor at " << monitor_path
               << ": " << monitor_socket->StrError();
    return false;
  }
  auto request = LauncherAction::kStop;
  auto bytes_sent = monitor_socket->Send(&request, sizeof(request), 0);
  if (bytes_sent < 0) {
    LOG(ERROR) << "Error sending launcher monitor the stop command: "
               << monitor_socket->StrError();
    return false;
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
    return false;
  }
  if (selected == 0) {
    LOG(ERROR) << "Timeout expired waiting for launcher monitor to respond";
    return false;
  }
  LauncherResponse response;
  auto bytes_recv = monitor_socket->Recv(&response, sizeof(response), 0);
  if (bytes_recv < 0) {
    LOG(ERROR) << "Error receiving response from launcher monitor: "
               << monitor_socket->StrError();
    return false;
  }
  if (response != LauncherResponse::kSuccess) {
    LOG(ERROR) << "Received '" << static_cast<char>(response)
               << "' response from launcher monitor";
    return false;
  }
  LOG(INFO) << "Successfully stopped device " << instance.adb_ip_and_port();
  return true;
}

int StopInstance(const CuttlefishConfig& config,
                 const CuttlefishConfig::InstanceSpecific& instance) {
  bool res = CleanStopInstance(instance);
  if (!res) {
    return FallBackStop(PathsForInstance(config, instance));
  }
  return 0;
}

/// Send a StopSession request to allocd
void ReleaseAllocdResources(SharedFD allocd_sock, uint32_t session_id) {
  if (!allocd_sock->IsOpen() || session_id == -1) {
    return;
  }
  Json::Value config;
  Json::Value request_list;
  Json::Value req;
  req["request_type"] =
      ReqTyToStr(RequestType::StopSession);
  req["session_id"] = session_id;
  request_list.append(req);
  config["config_request"]["request_list"] = request_list;
  SendJsonMsg(allocd_sock, config);
  auto resp_opt = RecvJsonMsg(allocd_sock);
  if (!resp_opt.has_value()) {
    LOG(ERROR) << "Bad response from allocd";
    return;
  }
  auto resp = resp_opt.value();
  LOG(INFO) << "Stop Session operation: " << resp["config_status"];
}

int StopCvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return FallBackStop(FallbackPaths());
  }

  int ret = 0;
  for (const auto& instance : config->Instances()) {
    auto session_id = instance.session_id();
    int exit_status = StopInstance(*config, instance);
    if (exit_status == 0 && instance.use_allocd()) {
      // only release session resources if the instance was stopped
      SharedFD allocd_sock =
          SharedFD::SocketLocalClient(kDefaultLocation, false, SOCK_STREAM);
      if (!allocd_sock->IsOpen()) {
        LOG(ERROR) << "Unable to connect to allocd on "
                   << kDefaultLocation << ": "
                   << allocd_sock->StrError();
      }

      ReleaseAllocdResources(allocd_sock, session_id);
    }
    ret |= exit_status;
  }

  return ret;
}

} // namespace
} // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::StopCvdMain(argc, argv);
}

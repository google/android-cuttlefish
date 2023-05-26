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

#include <sys/types.h>

#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <android-base/strings.h>
#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/allocd/request.h"
#include "host/libs/allocd/utils.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

std::set<std::string> FallbackDirs() {
  std::set<std::string> paths;
  std::string parent_path = StringFromEnv("HOME", ".");
  paths.insert(parent_path + "/cuttlefish_assembly");

  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(parent_path.c_str()), closedir);
  for (auto entity = readdir(dir.get()); entity != nullptr; entity = readdir(dir.get())) {
    std::string subdir(entity->d_name);
    if (!android::base::StartsWith(subdir, "cuttlefish_runtime.")) {
      continue;
    }
    paths.insert(parent_path + "/" + subdir);
  }
  return paths;
}

std::set<std::string> DirsForInstance(
    const CuttlefishConfig& config,
    const CuttlefishConfig::InstanceSpecific instance) {
  return {
      config.assembly_dir(),
      instance.instance_dir(),
      instance.instance_uds_dir(),
  };
}

// Gets a set of the possible process groups of a previous launch
std::set<pid_t> GetCandidateProcessGroups(const std::set<std::string>& dirs) {
  std::stringstream cmd;
  cmd << "lsof -t 2>/dev/null";
  for (const auto& dir : dirs) {
    cmd << " +D " << dir;
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

int FallBackStop(const std::set<std::string>& dirs) {
  auto exit_code = 1; // Having to fallback is an error

  auto process_groups = GetCandidateProcessGroups(dirs);
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

Result<void> CleanStopInstance(
    const CuttlefishConfig::InstanceSpecific& instance_config,
    const std::int32_t wait_for_launcher) {
  SharedFD monitor_socket = CF_EXPECT(
      GetLauncherMonitorFromInstance(instance_config, wait_for_launcher));

  LOG(INFO) << "Requesting stop";
  CF_EXPECT(WriteLauncherAction(monitor_socket, LauncherAction::kStop));
  CF_EXPECT(WaitForRead(monitor_socket, wait_for_launcher));
  LauncherResponse stop_response =
      CF_EXPECT(ReadLauncherResponse(monitor_socket));
  CF_EXPECT(
      stop_response == LauncherResponse::kSuccess,
      "Received `" << static_cast<char>(stop_response)
                   << "` response from launcher monitor for status request");

  LOG(INFO) << "Successfully stopped device " << instance_config.instance_name()
            << ": " << instance_config.adb_ip_and_port();
  return {};
}

int StopInstance(const CuttlefishConfig& config,
                 const CuttlefishConfig::InstanceSpecific& instance,
                 const std::int32_t wait_for_launcher) {
  auto result = CleanStopInstance(instance, wait_for_launcher);
  if (!result.ok()) {
    LOG(ERROR) << "Clean stop failed: " << result.error().Message();
    LOG(DEBUG) << "Clean stop failed: " << result.error().Trace();
    return FallBackStop(DirsForInstance(config, instance));
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

struct FlagVaules {
  std::int32_t wait_for_launcher;
  bool clear_instance_dirs;
  bool helpxml;
};

FlagVaules GetFlagValues(int argc, char** argv) {
  std::int32_t wait_for_launcher = 5;
  bool clear_instance_dirs = false;
  std::vector<Flag> flags;
  flags.emplace_back(
      GflagsCompatFlag("wait_for_launcher", wait_for_launcher)
          .Help("How many seconds to wait for the launcher to respond to the "
                "status command. A value of zero means wait indefinitely"));
  flags.emplace_back(
      GflagsCompatFlag("clear_instance_dirs", clear_instance_dirs)
          .Help("If provided, deletes the instance dir after attempting to "
                "stop each instance."));
  flags.emplace_back(HelpFlag(flags));
  bool helpxml = false;
  flags.emplace_back(HelpXmlFlag(flags, std::cout, helpxml));
  flags.emplace_back(UnexpectedArgumentGuard());
  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  auto parse_result = ParseFlags(flags, args);
  CHECK(parse_result || helpxml) << "Could not process command line flags.";

  return {wait_for_launcher, clear_instance_dirs, helpxml};
}

int StopCvdMain(const std::int32_t wait_for_launcher,
                const bool clear_instance_dirs) {
  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Failed to obtain config object";
    return FallBackStop(FallbackDirs());
  }

  int exit_code = 0;
  auto instances = config->Instances();
  std::vector<std::future<int>> exit_state_futures;
  exit_state_futures.reserve(instances.size());
  for (const auto& instance : instances) {
    std::future<int> exit_code_from_thread = std::async(
        std::launch::async,
        [&instance, &config, &wait_for_launcher,
         &clear_instance_dirs]() -> int {
          int exit_status = StopInstance(*config, instance, wait_for_launcher);
          {
            auto session_id = instance.session_id();
            if (exit_status == 0 && instance.use_allocd()) {
              // only release session resources if the instance was stopped
              SharedFD allocd_sock = SharedFD::SocketLocalClient(
                  kDefaultLocation, false, SOCK_STREAM);
              if (!allocd_sock->IsOpen()) {
                LOG(ERROR) << "Unable to connect to allocd on "
                           << kDefaultLocation << ": "
                           << allocd_sock->StrError();
              }
              ReleaseAllocdResources(allocd_sock, session_id);
            }
          }

          if (clear_instance_dirs && DirectoryExists(instance.instance_dir())) {
            LOG(INFO) << "Deleting instance dir " << instance.instance_dir();
            if (!RecursivelyRemoveDirectory(instance.instance_dir())) {
              LOG(ERROR) << "Unable to rmdir " << instance.instance_dir();
            }
          }
          return exit_status;
        });
    exit_state_futures.push_back(std::move(exit_code_from_thread));
  }
  for (auto& exit_status : exit_state_futures) {
    exit_code |= exit_status.get();
  }
  return exit_code;
}

} // namespace
} // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  const auto [wait_for_launcher, clear_instance_dirs, helpxml] =
      cuttlefish::GetFlagValues(argc, argv);

  if (helpxml) {
    /*
     * b/269925398
     *
     * CHECK(false) should not be executed if --helpxml is given.
     * The return code does not have to be the same. It is good if
     * CHECK(false) and --helpxml return the same return code.
     */
    return 134;
  }
  return cuttlefish::StopCvdMain(wait_for_launcher, clear_instance_dirs);
}

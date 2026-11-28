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

#include "cuttlefish/host/commands/cvd/instances/stop.h"

#include <signal.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "android-base/file.h"
#include "fmt/core.h"
#include "fmt/ranges.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/host/commands/cvd/instances/config_path.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/host/commands/cvd/instances/run_cvd_proc_collector.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/process/proc_file_utils.h"
#include "cuttlefish/process/subprocess_options.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

static Command CreateStopCvdCommand(
    const std::string& stopper_path,
    const std::unordered_map<std::string, std::string>& envs,
    const std::vector<std::string>& args) {
  Command command(android::base::Basename(stopper_path));
  command.SetExecutable(stopper_path);
  for (const auto& arg : args) {
    command.AddParameter(arg);
  }
  command.SetEnvironment({});
  for (const auto& [key, value] : envs) {
    command.AddEnvironmentVariable(key, value);
  }
  return command;
}

Result<void> RunStopCvdAll(bool clear_runtime_dirs) {
  std::vector<GroupProcInfo> group_infos = CF_EXPECT(CollectRunCvdGroups());
  LOG(INFO) << "Found " << group_infos.size()
            << " untracked running instance groups";
  for (const GroupProcInfo& group_info : group_infos) {
    auto stop_cvd_result = RunStopCvd(StopCvdParams{
        .bin_path = group_info.stop_cvd_path_,
        .home_dir = group_info.home_,
        .wait_for_launcher_secs = 5,
        .clear_runtime_dirs = clear_runtime_dirs,
    });
    if (!stop_cvd_result.has_value()) {
      LOG(ERROR) << stop_cvd_result.error();
      continue;
    }
  }
  return {};
}

static bool IsStillRunCvd(const pid_t pid) {
  if (pid <= 0 || kill(pid, 0) != 0) {
    return false;
  }
  auto owner_result = OwnerUid(pid);
  if (!owner_result.has_value() || (getuid() != *owner_result)) {
    return false;
  }
  auto extract_proc_info_result = ExtractProcInfo(pid);
  if (!extract_proc_info_result.has_value()) {
    return false;
  }
  return (android::base::Basename(
              extract_proc_info_result->actual_exec_path_) == "run_cvd");
}

Result<void> SendSignal(pid_t pid) {
  int kill_res = kill(pid, SIGKILL);
  CF_EXPECTF(kill_res == 0, "Failed to kill {}: {}", pid, StrError());
  return {};
}

Result<void> SendSignal(const GroupProcInfo& group_info) {
  std::vector<pid_t> failed_pids;
  for (const auto& [unused, instance] : group_info.instances_) {
    for (const auto parent_run_cvd_pid : instance.parent_run_cvd_pids_) {
      if (!IsStillRunCvd(parent_run_cvd_pid)) {
        continue;
      }
      LOG(INFO) << "Sending SIGKILL to process " << parent_run_cvd_pid;
      if (SendSignal(parent_run_cvd_pid).has_value()) {
        VLOG(1) << "Successfully SIGKILL'ed " << parent_run_cvd_pid;
      } else {
        failed_pids.push_back(parent_run_cvd_pid);
      }
    }
  }
  CF_EXPECTF(failed_pids.empty(),
             "Some run_cvd processes were not killed: [{}]",
             fmt::join(failed_pids, ", "));
  return {};
}

Result<void> ResetGroupLocks(const GroupProcInfo& group_info,
                             InstanceLockFileManager& lock_manager) {
  bool all_success = true;
  for (const auto& [id, unused] : group_info.instances_) {
    if (id == 0) {
      continue;
    }
    if (Result<void> res = lock_manager.ResetLockFile(id); !res.has_value()) {
      all_success = false;
      LOG(ERROR) << "Failed to reset lock file for instance " << id << ": "
                 << res.error();
    }
  }
  CF_EXPECT(all_success == true);
  return {};
}

Result<void> ForcefullyStopGroup(const GroupProcInfo& group,
                                 InstanceLockFileManager* lock_manager) {
  Result<void> signal_res = SendSignal(group);
  if (lock_manager != nullptr) {
    const Result<void> reset_res = ResetGroupLocks(group, *lock_manager);
    if (!reset_res.has_value()) {
      LOG(ERROR) << "Tried to reset instance lock file for the group rooted at "
                    "HOME="
                 << group.home_ << " but failed.";
    }
  }
  CF_EXPECTF(std::move(signal_res),
             "Tried SIGKILL to a group of run_cvd processes rooted at "
             "HOME={} but failed",
             group.home_);
  return {};
}

Result<void> KillAllRunCvds() {
  auto run_cvd_pids = CF_EXPECT(CollectRunCvdProcesses());
  if (run_cvd_pids.empty()) {
    return {};
  }
  LOG(INFO) << run_cvd_pids.size()
            << " run_cvd processes still remain, will stop forcefully";
  for (pid_t group_pid : run_cvd_pids) {
    if (Result<void> result = SendSignal(group_pid); !result.has_value()) {
      LOG(ERROR) << result.error();
    }
  }
  return {};
}

}  // namespace

Result<void> KillAllCuttlefishInstances(bool clear_runtime_dirs,
                                        InstanceLockFileManager* lock_manager) {
  if (Result<void> res = RunStopCvdAll(clear_runtime_dirs); !res.has_value()) {
    LOG(ERROR) << res.error();
  }

  if (Result<void> res = KillAllRunCvds(); !res.has_value()) {
    LOG(ERROR) << res.error();
  }

  if (lock_manager != nullptr) {
    if (Result<void> res = lock_manager->ResetAllOwnedLocks();
        !res.has_value()) {
      LOG(ERROR) << res.error();
    }
  }

  return {};
}

Result<void> ForcefullyStopGroup(const uid_t any_id_in_group,
                                 InstanceLockFileManager* lock_manager) {
  for (const GroupProcInfo& group_info : CF_EXPECT(CollectRunCvdGroups())) {
    if (!Contains(group_info.instances_,
                  static_cast<unsigned>(any_id_in_group))) {
      continue;
    }
    CF_EXPECT(ForcefullyStopGroup(group_info, lock_manager));
  }
  // run_cvd is not created yet as.. ctrl+C was in assembly phase, etc
  return {};
}

Result<void> RunStopCvd(StopCvdParams params) {
  // stop_cvd is located at $ANDROID_HOST_OUT/bin/stop_cvd
  const std::string android_host_out =
      android::base::Dirname(android::base::Dirname(params.bin_path));
  const std::unordered_map<std::string, std::string> stop_cvd_envs = {
      std::make_pair("HOME", params.home_dir),
      std::make_pair(kAndroidHostOut, android_host_out),
      std::make_pair(kAndroidSoongHostOut, android_host_out),
      std::make_pair(kCuttlefishConfigEnvVarName,
                     CF_EXPECT(GetCuttlefishConfigPath(params.home_dir))),
  };

  std::string wait_flag =
      fmt::format("--wait_for_launcher={}", params.wait_for_launcher_secs);
  std::vector<std::string> args = {wait_flag};
  if (params.clear_runtime_dirs) {
    args.emplace_back("--clear_instance_dirs=true");
  }
  if (!params.instance_nums.empty()) {
    args.emplace_back(fmt::format("--instance_nums={}",
                                  fmt::join(params.instance_nums, ",")));
  }

  const SubprocessOptions options = SubprocessOptions().InGroup(true);
  Result<std::string> cmd_res = RunAndCaptureStdout(
      CreateStopCvdCommand(params.bin_path, stop_cvd_envs, args), options);
  if (cmd_res.has_value()) {
    return {};
  }
  /**
   * --clear_instance_dirs or --instance_nums may not be available in old
   * branches. This causes stop_cvd to terminate with a non-zero exit code due
   * to a parsing error. Try again without that flag.
   */
  if (!params.clear_runtime_dirs && !params.instance_nums.empty()) {
    CF_EXPECT(std::move(cmd_res));
  }
  LOG(ERROR) << "--clear_instance_dirs or --instance_nums is not supported.";
  LOG(ERROR) << "Trying again without it";

  CF_EXPECT(RunAndCaptureStdout(
      CreateStopCvdCommand(params.bin_path, stop_cvd_envs, {wait_flag}),
      options));
  return {};
}
}  // namespace cuttlefish

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

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
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

Result<std::set<unsigned>> RunStopCvdAll(bool clear_runtime_dirs) {
  std::vector<GroupProcInfo> group_infos = CF_EXPECT(CollectRunCvdGroups());
  LOG(INFO) << "Found " << group_infos.size()
            << " untracked running instance groups";
  std::set<unsigned> instance_ids;
  for (const GroupProcInfo& group_info : group_infos) {
    for (const auto& [id, _] : group_info.instances_) {
      if (id > 0) {
        instance_ids.insert(id);
      }
    }
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
  return instance_ids;
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

Result<void> ResetInstanceLocks(const std::set<unsigned>& instance_ids) {
  InstanceLockFileManager lock_manager(InstanceLocksPath());
  bool all_success = true;
  for (unsigned id : instance_ids) {
    if (id == 0) {
      continue;
    }
    if (Result<void> res = lock_manager.RemoveLockFile(id); res.has_value()) {
      VLOG(0) << "Reset instance lock: " << id;
    } else {
      all_success = false;
      LOG(ERROR) << "Failed to reset lock file for instance " << id << ": "
                 << res.error();
    }
  }
  CF_EXPECT(all_success == true);
  return {};
}

Result<void> ResetGroupLocks(const GroupProcInfo& group_info) {
  std::set<unsigned> instance_ids;
  for (const auto& [id, _] : group_info.instances_) {
    if (id > 0) {
      instance_ids.insert(id);
    }
  }
  CF_EXPECT(ResetInstanceLocks(instance_ids));
  return {};
}

Result<void> ForcefullyStopGroup(const GroupProcInfo& group) {
  auto signal_res = SendSignal(group);
  auto reset_res = ResetGroupLocks(group);
  if (!reset_res.has_value()) {
    LOG(ERROR) << "Tried to reset instance lock file for the group rooted at "
                  "HOME="
               << group.home_ << " but failed.";
  }
  CF_EXPECTF(std::move(signal_res),
             "Tried SIGKILL to a group of run_cvd processes rooted at "
             "HOME={} but failed",
             group.home_);
  return {};
}

Result<std::set<unsigned>> KillAllRunCvds() {
  auto run_cvd_pids = CF_EXPECT(CollectRunCvdProcesses());
  std::set<unsigned> killed_ids;
  if (run_cvd_pids.empty()) {
    return killed_ids;
  }
  LOG(INFO) << run_cvd_pids.size()
            << " run_cvd processes still remain, will stop forcefully";
  for (pid_t group_pid : run_cvd_pids) {
    std::optional<unsigned> candidate_id;
    if (Result<std::unordered_map<std::string, std::string>> envs =
            GetEnvs(group_pid);
        envs.has_value()) {
      if (auto it = envs->find(kCuttlefishInstanceEnvVarName);
          it != envs->end()) {
        unsigned id = 0;
        if (absl::SimpleAtoi(it->second, &id) && id > 0) {
          candidate_id = id;
        }
      }
    }
    Result<void> result = SendSignal(group_pid);
    if (!result.has_value()) {
      LOG(ERROR) << result.error();
    }
    if (candidate_id.has_value() &&
        (result.has_value() || (kill(group_pid, 0) != 0 && errno == ESRCH))) {
      killed_ids.insert(*candidate_id);
    }
  }
  return killed_ids;
}

}  // namespace

Result<void> KillAllCuttlefishInstances(bool clear_runtime_dirs) {
  std::set<unsigned> untracked_ids;
  if (Result<std::set<unsigned>> res = RunStopCvdAll(clear_runtime_dirs);
      res.has_value()) {
    untracked_ids.insert(res->begin(), res->end());
  } else {
    LOG(ERROR) << res.error();
  }

  if (Result<std::set<unsigned>> res = KillAllRunCvds(); res.has_value()) {
    untracked_ids.insert(res->begin(), res->end());
  } else {
    LOG(ERROR) << res.error();
  }

  if (Result<void> res = ResetInstanceLocks(untracked_ids); !res.has_value()) {
    LOG(ERROR) << res.error();
  }

  InstanceLockFileManager lock_manager(InstanceLocksPath());
  if (Result<void> res = lock_manager.ResetAllOwnedLocks(); !res.has_value()) {
    LOG(ERROR) << "Failed to reset owned instance locks: " << res.error();
  }

  return {};
}

Result<void> ForcefullyStopGroup(const uid_t any_id_in_group) {
  for (const GroupProcInfo& group_info : CF_EXPECT(CollectRunCvdGroups())) {
    if (!Contains(group_info.instances_,
                  static_cast<unsigned>(any_id_in_group))) {
      continue;
    }
    CF_EXPECT(ForcefullyStopGroup(group_info));
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

  Result<std::string> cmd_res = RunAndCaptureStdout(
      CreateStopCvdCommand(params.bin_path, stop_cvd_envs, args));
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
      CreateStopCvdCommand(params.bin_path, stop_cvd_envs, {wait_flag})));
  return {};
}
}  // namespace cuttlefish

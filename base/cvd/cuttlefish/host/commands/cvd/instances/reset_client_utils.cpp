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

#include "cuttlefish/host/commands/cvd/instances/reset_client_utils.h"

#include <signal.h>

#include <iostream>  // std::endl
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/core.h>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/proc_file_utils.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/commands/cvd/instances/reset_client_utils.h"
#include "cuttlefish/host/commands/cvd/instances/run_cvd_proc_collector.h"
#include "cuttlefish/host/commands/cvd/legacy/run_server.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace {

static Command CreateStopCvdCommand(const std::string& stopper_path,
                                    const cvd_common::Envs& envs,
                                    const cvd_common::Args& args) {
  Command command(android::base::Basename(stopper_path));
  command.SetExecutable(stopper_path);
  for (const auto& arg : args) {
    command.AddParameter(arg);
  }
  for (const auto& [key, value] : envs) {
    command.UnsetFromEnvironment(key);
    command.AddEnvironmentVariable(key, value);
  }
  return command;
}

Result<void> RunStopCvd(const GroupProcInfo& group_info,
                        bool clear_runtime_dirs) {
  const auto& stopper_path = group_info.stop_cvd_path_;
  cvd_common::Envs stop_cvd_envs;
  stop_cvd_envs["HOME"] = group_info.home_;
  if (group_info.android_host_out_) {
    stop_cvd_envs[kAndroidHostOut] = group_info.android_host_out_.value();
    stop_cvd_envs[kAndroidSoongHostOut] = group_info.android_host_out_.value();
  } else {
    auto android_host_out = StringFromEnv(
        kAndroidHostOut,
        android::base::Dirname(android::base::GetExecutableDirectory()));
    stop_cvd_envs[kAndroidHostOut] = android_host_out;
    stop_cvd_envs[kAndroidSoongHostOut] = android_host_out;
  }

  if (clear_runtime_dirs) {
    Command first_stop_cvd = CreateStopCvdCommand(
        stopper_path, stop_cvd_envs, {"--clear_instance_dirs=true"});
    LOG(INFO) << "Running HOME=" << stop_cvd_envs.at("HOME") << " "
              << stopper_path << " --clear_instance_dirs=true";
    if (RunAndCaptureStdout(std::move(first_stop_cvd)).ok()) {
      LOG(INFO) << "\"" << stopper_path << " successfully "
                << "\" stopped instances at HOME=" << group_info.home_;
      return {};
    } else {
      LOG(ERROR) << "Failed to run " << stopper_path
                 << " --clear_instance_dirs=true";
      LOG(ERROR) << "Perhaps --clear_instance_dirs is not taken.";
      LOG(ERROR) << "Trying again without it";
    }
    // TODO(kwstephenkim): deletes manually if `stop_cvd --clear_instance_dirs`
    // failed.
  }
  Command second_stop_cvd =
      CreateStopCvdCommand(stopper_path, stop_cvd_envs, {});
  LOG(INFO) << "Running HOME=" << stop_cvd_envs.at("HOME") << " "
            << stopper_path;
  if (RunAndCaptureStdout(std::move(second_stop_cvd)).ok()) {
    LOG(INFO) << "\"" << stopper_path << " successfully "
              << "\" stopped instances at HOME=" << group_info.home_;
    return {};
  }
  return CF_ERRF("`HOME={} {}` Failed", group_info.home_,
                 group_info.stop_cvd_path_);
}

Result<void> RunStopCvdAll(bool clear_runtime_dirs) {
  for (const GroupProcInfo& group_info : CF_EXPECT(CollectRunCvdGroups())) {
    auto stop_cvd_result = RunStopCvd(group_info, clear_runtime_dirs);
    if (!stop_cvd_result.ok()) {
      LOG(ERROR) << stop_cvd_result.error().FormatForEnv();
      continue;
    }
  }
  return {};
}

static bool IsStillRunCvd(const pid_t pid) {
  std::string pid_dir = fmt::format("/proc/{}", pid);
  if (!FileExists(pid_dir)) {
    return false;
  }
  auto owner_result = OwnerUid(pid);
  if (!owner_result.ok() || (getuid() != *owner_result)) {
    return false;
  }
  auto extract_proc_info_result = ExtractProcInfo(pid);
  if (!extract_proc_info_result.ok()) {
    return false;
  }
  return (android::base::Basename(extract_proc_info_result->actual_exec_path_) ==
          "run_cvd");
}

Result<void> SendSignal(const GroupProcInfo& group_info) {
  std::vector<pid_t> failed_pids;
  for (const auto& [unused, instance] : group_info.instances_) {
    for (const auto parent_run_cvd_pid : instance.parent_run_cvd_pids_) {
      if (!IsStillRunCvd(parent_run_cvd_pid)) {
        continue;
      }
      if (kill(parent_run_cvd_pid, SIGKILL) == 0) {
        LOG(VERBOSE) << "Successfully SIGKILL'ed " << parent_run_cvd_pid;
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

Result<void> DeleteLockFile(const GroupProcInfo& group_info) {
  const std::string lock_dir = InstanceLocksPath();
  std::string lock_file_prefix = lock_dir;
  lock_file_prefix.append("/local-instance-");

  bool all_success = true;
  const auto& instances = group_info.instances_;
  for (const auto& [id, _] : instances) {
    std::stringstream lock_file_path_stream;
    lock_file_path_stream << lock_file_prefix << id << ".lock";
    auto lock_file_path = lock_file_path_stream.str();
    if (FileExists(lock_file_path) && !DirectoryExists(lock_file_path)) {
      if (RemoveFile(lock_file_path)) {
        LOG(DEBUG) << "Reset the lock file: " << lock_file_path;
      } else {
        all_success = false;
        LOG(ERROR) << "Failed to remove the lock file: " << lock_file_path;
      }
    }
  }
  CF_EXPECT(all_success == true);
  return {};
}

Result<void> ForcefullyStopGroup(const GroupProcInfo& group) {
  auto signal_res = SendSignal(group);
  auto delete_res = DeleteLockFile(group);
  if (!delete_res.ok()) {
    LOG(ERROR) << "Tried to delete instance lock file for the group rooted at "
                  "HOME="
               << group.home_ << " but failed.";
  }
  CF_EXPECTF(std::move(signal_res),
             "Tried SIGKILL to a group of run_cvd processes rooted at "
             "HOME={} but failed",
             group.home_);
  return {};
}

}  // namespace

Result<void> KillCvdServerProcess() {
  std::vector<pid_t> self_exe_pids =
      CF_EXPECT(CollectPidsByArgv0(kServerExecPath));
  if (self_exe_pids.empty()) {
    LOG(ERROR) << "cvd server is not running.";
    return {};
  }
  std::vector<pid_t> cvd_server_pids;
  /**
   * Finds processes whose executable path is kServerExecPath, and
   * that is owned by getuid(), and that has the "INTERNAL_server_fd"
   * in the arguments list.
   */
  for (const auto pid : self_exe_pids) {
    auto proc_info_result = ExtractProcInfo(pid);
    if (!proc_info_result.ok()) {
      LOG(ERROR) << "Failed to extract process info for pid " << pid;
      continue;
    }
    auto owner_uid_result = OwnerUid(pid);
    if (!owner_uid_result.ok()) {
      LOG(ERROR) << "Failed to find the uid for pid " << pid;
      continue;
    }
    if (getuid() != *owner_uid_result) {
      continue;
    }
    for (const auto& arg : proc_info_result->args_) {
      if (Contains(arg, kInternalServerFd)) {
        cvd_server_pids.push_back(pid);
        break;
      }
    }
  }
  if (cvd_server_pids.empty()) {
    LOG(ERROR)
        << "Cvd server process is not found. Perhaps, it is not running.";
    return {};
  }
  if (cvd_server_pids.size() > 1) {
    LOG(ERROR) << "There are " << cvd_server_pids.size() << " server processes "
               << "running while it should be up to 1.";
  }
  for (const auto pid : cvd_server_pids) {
    auto kill_ret = kill(pid, SIGKILL);
    if (kill_ret == 0) {
      LOG(ERROR) << "Cvd server process #" << pid << " is killed.";
    } else {
      LOG(ERROR) << "kill(" << pid << ", SIGKILL) failed.";
    }
  }
  return {};
}

Result<void> KillAllCuttlefishInstances(bool clear_runtime_dirs) {
  auto stop_cvd_result = RunStopCvdAll(clear_runtime_dirs);
  if (!stop_cvd_result.ok()) {
    LOG(ERROR) << stop_cvd_result.error().FormatForEnv();
  }
  for (const GroupProcInfo& group_info : CF_EXPECT(CollectRunCvdGroups())) {
    auto result = ForcefullyStopGroup(group_info);
    if (!result.ok()) {
      LOG(ERROR) << result.error().FormatForEnv();
    }
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

}  // namespace cuttlefish

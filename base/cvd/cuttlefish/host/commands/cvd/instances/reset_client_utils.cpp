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
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/core.h>
#include <fmt/ranges.h>  // NOLINT(misc-include-cleaner): version difference

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/proc_file_utils.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/commands/cvd/instances/config_path.h"
#include "cuttlefish/host/commands/cvd/instances/reset_client_utils.h"
#include "cuttlefish/host/commands/cvd/instances/run_cvd_proc_collector.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/config/config_constants.h"

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

Result<void> RunStopCvdCmd(const std::string& stopper_path,
                        const cvd_common::Envs& env,
                        const cvd_common::Args& args) {
  Command stop_cmd = CreateStopCvdCommand(stopper_path, env, args);

  LOG(INFO) << "Running " << stop_cmd.ToString();
  Result<std::string> cmd_res = RunAndCaptureStdout(std::move(stop_cmd));
  if (!cmd_res.ok()) {
    LOG(ERROR) << "Failed to run " << stopper_path;
    CF_EXPECT(std::move(cmd_res));
  }
  LOG(INFO) << "\"" << stopper_path << " successfully ";
  return {};
}

Result<void> RunStopCvdAll(bool clear_runtime_dirs) {
  for (const GroupProcInfo& group_info : CF_EXPECT(CollectRunCvdGroups())) {
    auto stop_cvd_result = RunStopCvd(StopCvdParams{
        .bin_path = group_info.stop_cvd_path_,
        .home_dir = group_info.home_,
        .wait_for_launcher_secs = 5,
        .clear_runtime_dirs = clear_runtime_dirs,
    });
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

Result<void> RunStopCvd(StopCvdParams params) {
  const auto& stopper_path = params.bin_path;
  cvd_common::Envs stop_cvd_envs;
  stop_cvd_envs["HOME"] = params.home_dir;
  // stop_cvd is located at $ANDROID_HOST_OUT/bin/stop_cvd
  std::string android_host_out =
      android::base::Dirname(android::base::Dirname(stopper_path));
  stop_cvd_envs[kAndroidHostOut] = android_host_out;
  stop_cvd_envs[kAndroidSoongHostOut] = android_host_out;
  auto config_file_path = CF_EXPECT(GetCuttlefishConfigPath(params.home_dir));
  stop_cvd_envs[kCuttlefishConfigEnvVarName] = config_file_path;
  cvd_common::Args args;
  std::string wait_flag =
      fmt::format("--wait_for_launcher={}", params.wait_for_launcher_secs);
  args.push_back(wait_flag);
  if (params.clear_runtime_dirs) {
    args.push_back("--clear_instance_dirs=true");
  }
  Result<void> cmd_res = RunStopCvdCmd(stopper_path, stop_cvd_envs, args);
  if (cmd_res.ok()) {
    return {};
  }
  /**
   * --clear_instance_dirs may not be available in old branches. This causes
   * stop_cvd to terminate with a non-zero exit code due to a parsing error. Try
   * again without that flag.
   */
  if (!params.clear_runtime_dirs) {
    CF_EXPECT(std::move(cmd_res));
  }
  // TODO(kwstephenkim): deletes manually if `stop_cvd --clear_instance_dirs`
  // failed.
  LOG(ERROR) << "Perhaps --clear_instance_dirs is not supported.";
  LOG(ERROR) << "Trying again without it";

  CF_EXPECT(RunStopCvdCmd(stopper_path, stop_cvd_envs, {wait_flag}));
  return {};
}
}  // namespace cuttlefish

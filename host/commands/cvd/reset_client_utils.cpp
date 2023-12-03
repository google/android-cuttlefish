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

#include "host/commands/cvd/reset_client_utils.h"

#include <signal.h>

#include <algorithm>
#include <cctype>
#include <iomanip>   // std::setw
#include <iostream>  // std::endl
#include <regex>
#include <sstream>
#include <unordered_set>

#include <android-base/file.h>
#include <android-base/parseint.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/proc_file_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/reset_client_utils.h"
#include "host/commands/cvd/run_cvd_proc_collector.h"
#include "host/commands/cvd/run_server.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {

Result<RunCvdProcessManager> RunCvdProcessManager::Get() {
  RunCvdProcessCollector run_cvd_collector =
      CF_EXPECT(RunCvdProcessCollector::Get());
  RunCvdProcessManager run_cvd_processes_manager(std::move(run_cvd_collector));
  return run_cvd_processes_manager;
}

RunCvdProcessManager::RunCvdProcessManager(RunCvdProcessCollector&& collector)
    : run_cvd_process_collector_(std::move(collector)) {}

static Command CreateStopCvdCommand(const std::string& stopper_path,
                                    const cvd_common::Envs& envs,
                                    const cvd_common::Args& args) {
  Command command(cpp_basename(stopper_path));
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

Result<void> RunCvdProcessManager::RunStopCvd(const GroupProcInfo& group_info,
                                              const bool clear_runtime_dirs) {
  const auto& stopper_path = group_info.stop_cvd_path_;
  int ret_code = 0;
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
    LOG(ERROR) << "Running HOME=" << stop_cvd_envs.at("HOME") << " "
               << stopper_path << " --clear_instance_dirs=true";
    std::string stdout_str;
    std::string stderr_str;
    ret_code = RunWithManagedStdio(std::move(first_stop_cvd), nullptr,
                                   std::addressof(stdout_str),
                                   std::addressof(stderr_str));
    // TODO(kwstephenkim): deletes manually if `stop_cvd --clear_instance_dirs`
    // failed.
  }
  if (!clear_runtime_dirs || ret_code != 0) {
    if (clear_runtime_dirs) {
      LOG(ERROR) << "Failed to run " << stopper_path
                 << " --clear_instance_dirs=true";
      LOG(ERROR) << "Perhaps --clear_instance_dirs is not taken.";
      LOG(ERROR) << "Trying again without it";
    }
    Command second_stop_cvd =
        CreateStopCvdCommand(stopper_path, stop_cvd_envs, {});
    LOG(ERROR) << "Running HOME=" << stop_cvd_envs.at("HOME") << " "
               << stopper_path;
    std::string stdout_str;
    std::string stderr_str;
    ret_code = RunWithManagedStdio(std::move(second_stop_cvd), nullptr,
                                   std::addressof(stdout_str),
                                   std::addressof(stderr_str));
  }
  if (ret_code != 0) {
    std::stringstream error;
    error << "HOME=" << group_info.home_
          << group_info.stop_cvd_path_ + " Failed.";
    return CF_ERR(error.str());
  }
  LOG(ERROR) << "\"" << stopper_path << " successfully "
             << "\" stopped instances at HOME=" << group_info.home_;
  return {};
}

Result<void> RunCvdProcessManager::RunStopCvdAll(
    const bool cvd_server_children_only, const bool clear_instance_dirs) {
  for (const auto& group_info : run_cvd_process_collector_.CfGroups()) {
    if (cvd_server_children_only && !group_info.is_cvd_server_started_) {
      continue;
    }
    auto stop_cvd_result = RunStopCvd(group_info, clear_instance_dirs);
    if (!stop_cvd_result.ok()) {
      LOG(ERROR) << stop_cvd_result.error().FormatForEnv();
      continue;
    }
  }
  return {};
}

static bool IsStillRunCvd(const pid_t pid) {
  std::string pid_dir = ConcatToString("/proc/", pid);
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
  return (cpp_basename(extract_proc_info_result->actual_exec_path_) ==
          "run_cvd");
}

Result<void> RunCvdProcessManager::SendSignals(
    const bool cvd_server_children_only) {
  auto recollected_run_cvd_pids = CF_EXPECT(CollectPidsByExecName("run_cvd"));
  std::unordered_set<pid_t> failed_pids;
  for (const auto& group_info : run_cvd_process_collector_.CfGroups()) {
    if (cvd_server_children_only && !group_info.is_cvd_server_started_) {
      continue;
    }
    for (const auto& [_, instance] : group_info.instances_) {
      const auto& pids = instance.pids_;
      for (const auto pid : pids) {
        if (!Contains(recollected_run_cvd_pids, pid)) {
          // pid is alive but reassigned to non-run_cvd process
          continue;
        }
        if (!IsStillRunCvd(pid)) {
          // pid is now assigned to a different process
          continue;
        }
        auto ret_sigkill = kill(pid, SIGKILL);
        if (ret_sigkill == 0) {
          LOG(ERROR) << "SIGKILL was delivered to pid #" << pid;
        } else {
          LOG(ERROR) << "SIGKILL was not delivered to pid #" << pid;
        }
        if (!IsStillRunCvd(pid)) {
          continue;
        }
        LOG(ERROR) << "Will still send SIGHUP as run_cvd #" << pid
                   << " has not been terminated by SIGKILL.";
        auto ret_sighup = kill(pid, SIGHUP);
        if (ret_sighup != 0) {
          LOG(ERROR) << "SIGHUP sent to process #" << pid << " but all failed.";
        }
        if (ret_sigkill != 0 && ret_sighup != 0) {
          failed_pids.insert(pid);
        }
      }
    }
  }
  std::stringstream error_msg_stream;
  error_msg_stream << "Some run_cvd processes were not killed: {";
  for (const auto& pid : failed_pids) {
    error_msg_stream << pid << ",";
  }
  auto error_msg = error_msg_stream.str();
  if (!failed_pids.empty()) {
    error_msg.pop_back();
  }
  error_msg.append("}");
  CF_EXPECT(failed_pids.empty(), error_msg);
  return {};
}

static Result<void> DeleteAllLockFiles(const std::string& lock_dir) {
  CF_EXPECT(DirectoryExists(lock_dir), lock_dir + " does not exist");
  const auto all_files =
      CF_EXPECT(DirectoryContents(lock_dir),
                "Failed pull out the contents of " + lock_dir);
  for (const auto& base_name : all_files) {
    const std::string file_in_lock_dir =
        ConcatToString(lock_dir, "/", base_name);
    std::regex lock_file_name_pattern("local[-]instance[-][0-9]+[.]lock");
    if (std::regex_match(base_name, lock_file_name_pattern)) {
      LOG(VERBOSE) << "Deleting " << file_in_lock_dir;
      if (!RemoveFile(file_in_lock_dir)) {
        // TODO(weihsu): demote the verbosity level to DEBUG
        // print ERROR only if the file belongs to the user
        LOG(ERROR) << "Failed to delete " << file_in_lock_dir;
      }
    }
  }
  return {};
}

void RunCvdProcessManager::DeleteLockFiles(
    const bool cvd_server_children_only) {
  const std::string lock_dir = "/tmp/acloud_cvd_temp";
  std::string lock_file_prefix = lock_dir;
  lock_file_prefix.append("/local-instance-");

  if (!cvd_server_children_only) {
    auto delete_all_result = DeleteAllLockFiles(lock_dir);
    if (!delete_all_result.ok()) {
      LOG(ERROR) << delete_all_result.error().FormatForEnv();
    }
    return;
  }

  for (const auto& group_info : run_cvd_process_collector_.CfGroups()) {
    if (!group_info.is_cvd_server_started_) {
      continue;
    }
    const auto& instances = group_info.instances_;
    for (const auto& [id, _] : instances) {
      std::stringstream lock_file_path_stream;
      lock_file_path_stream << lock_file_prefix << id << ".lock";
      auto lock_file_path = lock_file_path_stream.str();
      if (FileExists(lock_file_path) && !DirectoryExists(lock_file_path)) {
        if (RemoveFile(lock_file_path)) {
          LOG(ERROR) << "Reset the lock file: " << lock_file_path;
        } else {
          LOG(ERROR) << "Failed to reset lock file: " << lock_file_path;
        }
      }
    }
  }
}

Result<void> KillAllCuttlefishInstances(const DeviceClearOptions& options) {
  RunCvdProcessManager manager = CF_EXPECT(RunCvdProcessManager::Get());
  CF_EXPECT(manager.KillAllCuttlefishInstances(options.cvd_server_children_only,
                                               options.clear_instance_dirs));
  return {};
}

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
    if (kill_ret != 0) {
      LOG(ERROR) << "kill(" << pid << ", SIGKILL) failed.";
    } else {
      LOG(ERROR) << "Cvd server process #" << pid << " is killed.";
    }
  }
  return {};
}

Result<void> RunCvdProcessManager::KillAllCuttlefishInstances(
    const bool cvd_server_children_only, const bool clear_runtime_dirs) {
  auto stop_cvd_result =
      RunStopCvdAll(cvd_server_children_only, clear_runtime_dirs);
  if (!stop_cvd_result.ok()) {
    LOG(ERROR) << stop_cvd_result.error().FormatForEnv();
  }
  auto send_signals_result = SendSignals(cvd_server_children_only);
  if (!send_signals_result.ok()) {
    LOG(ERROR) << send_signals_result.error().FormatForEnv();
  }
  DeleteLockFiles(cvd_server_children_only);
  return {};
}

}  // namespace cuttlefish

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
#include <sstream>
#include <unordered_set>

#include <android-base/file.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/proc_file_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/reset_client_utils.h"

namespace cuttlefish {

static bool IsTrue(const std::string& value) {
  std::unordered_set<std::string> true_strings = {"y", "yes", "true"};
  std::string value_in_lower_case = value;
  /*
   * https://en.cppreference.com/w/cpp/string/byte/tolower
   *
   * char should be converted to unsigned char first.
   */
  std::transform(value_in_lower_case.begin(), value_in_lower_case.end(),
                 value_in_lower_case.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return Contains(true_strings, value_in_lower_case);
}

static Result<RunCvdProcInfo> AnalyzeRunCvdProcess(const pid_t pid) {
  auto envs = CF_EXPECT(GetEnvs(pid));
  RunCvdProcInfo info;
  info.pid_ = pid;
  CF_EXPECT(Contains(envs, "HOME"));
  info.pid_ = pid;
  info.home_ = envs.at("HOME");
  info.envs_ = std::move(envs);
  if (!Contains(info.envs_, kAndroidHostOut) &&
      !Contains(info.envs_, kAndroidSoongHostOut)) {
    const std::string server_host_out =
        android::base::Dirname(android::base::GetExecutableDirectory());
    info.envs_[kAndroidHostOut] = server_host_out;
    info.envs_[kAndroidSoongHostOut] = server_host_out;
  }

  if (Contains(info.envs_, kCvdMarkEnv) && IsTrue(info.envs_.at(kCvdMarkEnv))) {
    info.is_cvd_server_started_ = true;
  }

  std::vector<std::string> stop_bins{"cvd_internal_stop", "stop_cvd"};

  if (Contains(info.envs_, kAndroidHostOut)) {
    for (const auto& bin : stop_bins) {
      std::string internal_stop_path =
          ConcatToString(info.envs_.at(kAndroidHostOut), "/bin/", bin);
      if (!FileExists(internal_stop_path)) {
        continue;
      }
      info.stop_cvd_path_ = std::move(internal_stop_path);
      return info;
    }
  }

  for (const auto& bin : stop_bins) {
    std::string stop_cvd_path =
        ConcatToString(info.envs_.at(kAndroidSoongHostOut), "/bin/", bin);
    if (!FileExists(stop_cvd_path)) {
      continue;
    }
    info.stop_cvd_path_ = std::move(stop_cvd_path);
    return info;
  }

  return CF_ERR("cvd_internal_stop or stop_cvd cannot be found for "
                << " pid #" << pid);
}

static Command CreateStopCvdCommand(const std::string& stopper_path,
                                    const cvd_common::Envs& envs,
                                    const cvd_common::Args& args) {
  Command command(cpp_basename(stopper_path));
  command.SetExecutable(stopper_path);
  for (const auto& arg : args) {
    command.AddParameter(arg);
  }
  if (Contains(envs, "HOME")) {
    command.AddEnvironmentVariable("HOME", envs.at("HOME"));
  }
  if (Contains(envs, kAndroidHostOut)) {
    command.AddEnvironmentVariable(kAndroidHostOut, envs.at(kAndroidHostOut));
  }
  if (Contains(envs, kAndroidSoongHostOut)) {
    command.AddEnvironmentVariable(kAndroidSoongHostOut,
                                   envs.at(kAndroidSoongHostOut));
  }
  if (Contains(envs, kAndroidProductOut)) {
    command.AddEnvironmentVariable(kAndroidProductOut,
                                   envs.at(kAndroidProductOut));
  }
  return command;
}

Result<RunCvdProcessManager> RunCvdProcessManager::Get() {
  RunCvdProcessManager run_cvd_processes_manager;
  run_cvd_processes_manager.run_cvd_processes_ =
      CF_EXPECT(run_cvd_processes_manager.CollectInfo());
  return run_cvd_processes_manager;
}

Result<std::vector<RunCvdProcInfo>> RunCvdProcessManager::CollectInfo() {
  auto run_cvd_pids = CF_EXPECT(CollectPidsByExecName("run_cvd"));
  std::vector<RunCvdProcInfo> output;
  output.reserve(run_cvd_pids.size());
  for (const auto run_cvd_pid : run_cvd_pids) {
    auto run_cvd_info_result = AnalyzeRunCvdProcess(run_cvd_pid);
    if (!run_cvd_info_result.ok()) {
      LOG(ERROR) << "Failed to collect information for run_cvd at #"
                 << run_cvd_pid << std::endl
                 << run_cvd_info_result.error().Trace();
      continue;
    }
    output.push_back(*run_cvd_info_result);
  }
  return output;
}

void RunCvdProcessManager::ShowAll() {
  for (const auto& run_cvd_info : run_cvd_processes_) {
    std::string android_host_out;
    if (Contains(run_cvd_info.envs_, kAndroidHostOut)) {
      android_host_out = run_cvd_info.envs_.at(kAndroidHostOut);
    }
    std::string android_soong_host_out;
    if (Contains(run_cvd_info.envs_, kAndroidSoongHostOut)) {
      android_soong_host_out = run_cvd_info.envs_.at(kAndroidSoongHostOut);
    }

    std::cout << "[" << std::endl
              << std::left << "  " << std::setw(26)
              << "pid : " << run_cvd_info.pid_ << std::endl
              << "  " << std::setw(26) << "home : " << run_cvd_info.home_
              << std::endl
              << "  " << std::setw(26)
              << "ANDROID_HOST_OUT : " << android_host_out << std::endl
              << "  " << std::setw(26)
              << "ANDROID_SOONG_HOST_OUT : " << android_soong_host_out
              << std::endl
              << "  " << std::setw(26)
              << "stop cvd path : " << run_cvd_info.stop_cvd_path_ << std::endl
              << "  " << std::setw(26) << "By Cvd Server : "
              << (run_cvd_info.is_cvd_server_started_ ? "true" : "false")
              << std::endl;
  }
}

Result<void> RunCvdProcessManager::RunStopCvd(
    const RunCvdProcInfo& run_cvd_info) {
  const auto& stopper_path = run_cvd_info.stop_cvd_path_;
  Command first_stop_cvd = CreateStopCvdCommand(
      stopper_path, run_cvd_info.envs_, {"--clear_instance_dirs"});
  LOG(ERROR) << "Running HOME=" << run_cvd_info.envs_.at("HOME") << " "
             << stopper_path << " --clear_instance_dirs";
  std::string stdout_str;
  std::string stderr_str;
  auto ret_code = RunWithManagedStdio(std::move(first_stop_cvd), nullptr,
                                      std::addressof(stdout_str),
                                      std::addressof(stderr_str));
  if (ret_code != 0) {
    LOG(ERROR) << "Failed.";
    Command second_stop_cvd =
        CreateStopCvdCommand(stopper_path, run_cvd_info.envs_, {});
    LOG(ERROR) << "Trying HOME=" << run_cvd_info.envs_.at("HOME") << " "
               << stopper_path;
    std::string stdout_str;
    std::string stderr_str;
    ret_code = RunWithManagedStdio(std::move(second_stop_cvd), nullptr,
                                   std::addressof(stdout_str),
                                   std::addressof(stderr_str));
  }

  if (ret_code != 0) {
    std::stringstream error;
    error << "HOME=" << run_cvd_info.home_
          << run_cvd_info.stop_cvd_path_ + " Failed.";
    return CF_ERR(error.str());
  }
  LOG(ERROR) << "\"" << stopper_path
             << "\" stopped instances at HOME=" << run_cvd_info.home_;
  return {};
}

Result<void> RunCvdProcessManager::RunStopCvdForEach(
    const bool cvd_server_children_only) {
  std::unordered_set<std::string> cvd_stopped_home;
  for (const auto& run_cvd_info : run_cvd_processes_) {
    if (cvd_server_children_only && !run_cvd_info.is_cvd_server_started_) {
      continue;
    }
    const auto& home_dir = run_cvd_info.home_;
    if (Contains(cvd_stopped_home, home_dir)) {
      continue;
    }
    auto stop_cvd_result = RunStopCvd(run_cvd_info);
    if (!stop_cvd_result.ok()) {
      LOG(ERROR) << stop_cvd_result.error().Trace();
      continue;
    }
    cvd_stopped_home.insert(home_dir);
  }
  return {};
}

Result<void> RunCvdProcessManager::SendSignals(
    const bool cvd_server_children_only) {
  auto recollected_run_cvd_pids = CF_EXPECT(CollectPidsByExecName("run_cvd"));
  std::unordered_set<pid_t> failed_pids;
  for (const auto& run_cvd_info : run_cvd_processes_) {
    const auto pid = run_cvd_info.pid_;
    if (!Contains(recollected_run_cvd_pids, pid)) {
      // pid is alive but reassigned to non-run_cvd process
      continue;
    }
    if (!FileExists(ConcatToString("/proc/", pid))) {
      // pid was already killed during stop cvd
      continue;
    }
    if (cvd_server_children_only && !run_cvd_info.is_cvd_server_started_) {
      continue;
    }
    auto ret_sigkill = kill(pid, SIGKILL);
    auto ret_sighup = kill(pid, SIGHUP);
    if (ret_sigkill != 0 && ret_sighup != 0) {
      LOG(ERROR) << "Both SIGKILL and SIGHUP sent to process #" << pid
                 << " but all failed.";
      failed_pids.insert(pid);
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

Result<void> KillAllCuttlefishInstances(const bool cvd_server_children_only) {
  RunCvdProcessManager manager = CF_EXPECT(RunCvdProcessManager::Get());
  CF_EXPECT(manager.KillAllCuttlefishInstances(cvd_server_children_only));
  return {};
}

}  // namespace cuttlefish

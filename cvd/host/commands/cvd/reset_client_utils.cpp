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
#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <iostream>  // std::endl
#include <string>
#include <unordered_set>

#include <android-base/file.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/proc_file_utils.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/reset_client_utils.h"
#include "host/commands/cvd/types.h"

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

struct RunCvdProcInfo {
  pid_t pid_;
  std::string home_;
  cvd_common::Envs envs_;
  std::string stop_cvd_path_;
  bool is_cvd_server_started_;
};

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

static Result<void> RunStopCvdOnEach(const std::vector<pid_t>& run_cvd_pids,
                                     const bool check_cvd_server_mark) {
  for (const auto pid : run_cvd_pids) {
    auto run_cvd_info_result = AnalyzeRunCvdProcess(pid);
    if (!run_cvd_info_result.ok()) {
      LOG(ERROR) << "Failed to collect information for run_cvd at #" << pid
                 << std::endl
                 << run_cvd_info_result.error().Trace();
      continue;
    }
    auto run_cvd_info = std::move(*run_cvd_info_result);
    if (check_cvd_server_mark) {
      if (!Contains(run_cvd_info.envs_, kCvdMarkEnv)) {
        continue;
      }
      if (!IsTrue(run_cvd_info.envs_.at(kCvdMarkEnv))) {
        continue;
      }
    }

    const auto& stopper_path = run_cvd_info.stop_cvd_path_;
    Command first_stop_cvd = CreateStopCvdCommand(
        stopper_path, run_cvd_info.envs_, {"--clear_instance_dirs"});
    LOG(ERROR) << "Running HOME=" << run_cvd_info.envs_["HOME"] << " "
               << stopper_path << " --clear_instance_dirs";
    std::string stdout_str;
    std::string stderr_str;
    auto ret_code = RunWithManagedStdio(std::move(first_stop_cvd), nullptr,
                                        std::addressof(stdout_str),
                                        std::addressof(stderr_str));
    if (ret_code != 0) {
      Command second_stop_cvd =
          CreateStopCvdCommand(stopper_path, run_cvd_info.envs_, {});
      LOG(ERROR) << "Running HOME=" << run_cvd_info.envs_["HOME"] << " "
                 << stopper_path;
      std::string stdout_str;
      std::string stderr_str;
      ret_code = RunWithManagedStdio(std::move(second_stop_cvd), nullptr,
                                     std::addressof(stdout_str),
                                     std::addressof(stderr_str));
    }

    if (ret_code != 0) {
      LOG(ERROR) << "\"" << stopper_path << "\""
                 << " failed to stop instances at HOME=" << run_cvd_info.home_;
      if (!stderr_str.empty()) {
        LOG(ERROR) << "The error was: " << stderr_str;
      }
      continue;
    }

    LOG(ERROR) << "\"" << stopper_path
               << "\" stopped instances at HOME=" << run_cvd_info.home_;
  }
  return {};
}

Result<void> KillAllCuttlefishInstances(const bool cvd_server_children_only) {
  auto run_cvd_pids = CF_EXPECT(CollectPidsByExecName("run_cvd"));
  auto graceful_stop_result =
      RunStopCvdOnEach(run_cvd_pids, cvd_server_children_only);
  if (!graceful_stop_result.ok()) {
    LOG(ERROR) << "Graceful stop failed. The log is here: \n"
               << graceful_stop_result.error().Trace();
  }
  LOG(ERROR) << "Sending SIGKILL to run_cvd processes.";
  for (const auto pid : run_cvd_pids) {
    auto ret_sighup = kill(pid, SIGHUP);
    auto ret_sigkill = kill(pid, SIGKILL);
    if (ret_sigkill != 0 && ret_sighup != 0) {
      LOG(ERROR) << "Both SIGKILL and SIGHUP sent to process #" << pid
                 << " but all failed.";
    }
  }
  auto recollect_run_cvd_pids = CF_EXPECT(CollectPidsByExecName("run_cvd"));
  CF_EXPECT(recollect_run_cvd_pids.empty());
  return {};
}

}  // namespace cuttlefish

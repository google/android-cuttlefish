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

#include "host/commands/cvd/run_cvd_proc_collector.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/proc_file_utils.h"
#include "host/commands/cvd/common_utils.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace {

struct RunCvdProcInfo {
  pid_t pid_;
  std::string home_;
  std::string exec_path_;
  std::unordered_map<std::string, std::string> envs_;
  std::vector<std::string> cmd_args_;
  std::string stop_cvd_path_;
  bool is_cvd_server_started_;
  std::optional<std::string> android_host_out_;
  unsigned id_;
  uid_t real_owner_uid_;
};

bool IsTrue(std::string value) {
  std::unordered_set<std::string> true_strings = {"y", "yes", "true"};
  for (auto& c : value) {
    c = std::tolower(static_cast<unsigned char>(c));
  }
  return Contains(true_strings, value);
}

static Result<std::string> SearchFilesInPath(
    const std::string& dir_path, const std::vector<std::string>& files) {
  for (const auto& bin : files) {
    std::string file_path = ConcatToString(dir_path, "/", bin);
    if (!FileExists(file_path)) {
      continue;
    }
    return file_path;
  }
  std::stringstream error;
  error << "cvd_internal_stop/stop_cvd does not exist in "
        << "the host tools path: " << dir_path << ".";
  return CF_ERR(error.str());
}

static Result<std::string> StopCvdPath(const RunCvdProcInfo& info) {
  std::vector<std::string> stop_bins{"cvd_internal_stop", "stop_cvd"};
  if (info.android_host_out_) {
    auto result =
        SearchFilesInPath(info.android_host_out_.value() + "/bin", stop_bins);
    if (result.ok()) {
      return *result;
    }
    LOG(ERROR) << result.error().FormatForEnv();
  } else {
    LOG(ERROR) << "run_cvd host tool directory was not able to be guessed.";
  }
  LOG(ERROR) << "Falling back to use the cvd executable path";
  const std::string cvd_dir =
      android::base::Dirname(android::base::GetExecutableDirectory());
  return SearchFilesInPath(cvd_dir, stop_bins);
}

static std::optional<std::string> HostOut(const cvd_common::Envs& envs) {
  if (Contains(envs, kAndroidHostOut)) {
    return envs.at(kAndroidHostOut);
  }
  if (Contains(envs, kAndroidSoongHostOut)) {
    return envs.at(kAndroidSoongHostOut);
  }
  return std::nullopt;
}

Result<RunCvdProcInfo> ExtractRunCvdInfo(const pid_t pid) {
  auto proc_info = CF_EXPECT(ExtractProcInfo(pid));
  RunCvdProcInfo info;
  info.pid_ = proc_info.pid_;
  info.real_owner_uid_ = proc_info.real_owner_;
  info.exec_path_ = proc_info.actual_exec_path_;
  info.cmd_args_ = std::move(proc_info.args_);
  info.envs_ = std::move(proc_info.envs_);
  CF_EXPECT(Contains(info.envs_, "HOME"));
  info.home_ = info.envs_.at("HOME");
  info.android_host_out_ = HostOut(info.envs_);

  CF_EXPECT(Contains(info.envs_, kCuttlefishInstanceEnvVarName));

  CF_EXPECT(android::base::ParseUint(
      info.envs_.at(kCuttlefishInstanceEnvVarName), &info.id_));

  if (Contains(info.envs_, kCvdMarkEnv) && IsTrue(info.envs_.at(kCvdMarkEnv))) {
    info.is_cvd_server_started_ = true;
  }

  info.stop_cvd_path_ =
      CF_EXPECTF(StopCvdPath(info),
                 "cvd_internal_stop or stop_cvd cannot be found for "
                 "pid #{}",
                 pid);
  return info;
}

Result<std::vector<RunCvdProcInfo>> ExtractAllRunCvdInfo(
    std::optional<uid_t> uid) {
  std::vector<RunCvdProcInfo> run_cvd_procs_of_uid;
  auto run_cvd_pids = CF_EXPECT(CollectPidsByExecName("run_cvd"), getuid());
  std::vector<ProcInfo> run_cvd_proc_infos;
  run_cvd_proc_infos.reserve(run_cvd_pids.size());
  for (const auto run_cvd_pid : run_cvd_pids) {
    auto proc_info_result = ExtractRunCvdInfo(run_cvd_pid);
    if (!proc_info_result.ok()) {
      LOG(DEBUG) << "Failed to fetch run_cvd process info for " << run_cvd_pid;
      // perhaps, not my process
      continue;
    }
    if (uid && uid.value() != proc_info_result->real_owner_uid_) {
      LOG(DEBUG) << "run_cvd process " << run_cvd_pid << " does not belong to "
                 << uid.value() << " so skipped.";
      continue;
    }
    run_cvd_procs_of_uid.emplace_back(std::move(*proc_info_result));
  }
  return run_cvd_procs_of_uid;
}

}  // namespace

Result<RunCvdProcessCollector> RunCvdProcessCollector::Get() {
  RunCvdProcessCollector run_cvd_processes_collector;
  run_cvd_processes_collector.cf_groups_ =
      CF_EXPECT(run_cvd_processes_collector.CollectInfo());
  return run_cvd_processes_collector;
}

Result<std::vector<RunCvdProcessCollector::GroupProcInfo>>

RunCvdProcessCollector::CollectInfo() {
  auto run_cvd_pids = CF_EXPECT(CollectPidsByExecName("run_cvd"));
  std::vector<RunCvdProcInfo> run_cvd_infos =
      CF_EXPECT(ExtractAllRunCvdInfo(getuid()));

  // home --> group map
  std::unordered_map<std::string, GroupProcInfo> groups;
  for (auto& run_cvd_info : run_cvd_infos) {
    const auto home = run_cvd_info.home_;
    if (!Contains(groups, home)) {
      // create using a default constructor
      auto& group = (groups[home] = GroupProcInfo());
      group.home_ = home;
      group.exec_path_ = run_cvd_info.exec_path_;
      group.stop_cvd_path_ = run_cvd_info.stop_cvd_path_;
      group.is_cvd_server_started_ = run_cvd_info.is_cvd_server_started_;
      group.android_host_out_ = run_cvd_info.android_host_out_;
    }
    auto& id_instance_map = groups[home].instances_;
    if (!Contains(id_instance_map, run_cvd_info.id_)) {
      id_instance_map[run_cvd_info.id_] = GroupProcInfo::InstanceInfo{
          .pids_ = std::set<pid_t>{run_cvd_info.pid_},
          .envs_ = std::move(run_cvd_info.envs_),
          .cmd_args_ = std::move(run_cvd_info.cmd_args_),
          .id_ = run_cvd_info.id_};
      continue;
    }
    // this is the other run_cvd process under the same instance i
    id_instance_map[run_cvd_info.id_].pids_.insert(run_cvd_info.pid_);
  }
  std::vector<GroupProcInfo> output;
  output.reserve(groups.size());
  for (auto& [_, group] : groups) {
    output.emplace_back(std::move(group));
  }
  return output;
}

}  // namespace cuttlefish

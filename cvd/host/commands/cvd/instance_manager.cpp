/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/instance_manager.h"

#include <map>
#include <mutex>
#include <thread>

#include <android-base/file.h>
#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<std::string> InstanceManager::GetCuttlefishConfigPath(
    const std::string& home) {
  return selector::GetCuttlefishConfigPath(home);
}

InstanceManager::InstanceManager(InstanceLockFileManager& lock_manager)
    : lock_manager_(lock_manager) {}

selector::InstanceDatabase& InstanceManager::GetInstanceDB(const uid_t uid) {
  if (!Contains(instance_dbs_, uid)) {
    instance_dbs_.try_emplace(uid);
  }
  return instance_dbs_[uid];
}

bool InstanceManager::HasInstanceGroups(const uid_t uid) {
  std::lock_guard lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  return !instance_db.IsEmpty();
}

Result<void> InstanceManager::SetInstanceGroup(
    const uid_t uid, const InstanceManager::InstanceGroupDir& dir,
    const InstanceGroupInfo& info) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  // for now, the group name is determined automatically by the instance_db_
  CF_EXPECT(instance_db.AddInstanceGroup(dir, info.host_binaries_dir));
  auto searched_group =
      CF_EXPECT(instance_db.FindGroup({selector::kHomeField, dir}));
  for (auto i : info.instances) {
    const std::string default_instance_name = std::to_string(i);
    instance_db.AddInstance(searched_group.Get(), i, default_instance_name);
  }
  return {};
}

void InstanceManager::RemoveInstanceGroup(
    const uid_t uid, const InstanceManager::InstanceGroupDir& dir) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  auto result = instance_db.FindGroup({selector::kHomeField, dir});
  if (!result.ok()) return;
  auto group = *result;
  instance_db.RemoveInstanceGroup(group);
}

Result<InstanceManager::InstanceGroupInfo>
InstanceManager::GetInstanceGroupInfo(
    const uid_t uid, const InstanceManager::InstanceGroupDir& dir) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  auto group = CF_EXPECT(instance_db.FindGroup({selector::kHomeField, dir}));
  InstanceGroupInfo info;
  info.host_binaries_dir = group.Get().HostBinariesDir();
  const auto& instances = group.Get().Instances();
  for (const auto& instance : instances) {
    CF_EXPECT(instance != nullptr);
    info.instances.insert(instance->InstanceId());
  }
  return {info};
}

void InstanceManager::IssueStatusCommand(
    const SharedFD& out, const SharedFD& err,
    const std::string& config_file_path,
    const selector::LocalInstanceGroup& group) {
  // Reads CuttlefishConfig::instance_names(), which must remain stable
  // across changes to config file format (within server_constants.h major
  // version).
  auto config = CuttlefishConfig::GetFromFile(config_file_path);
  if (!config) {
    return;
  }
  Command command(group.HostBinariesDir() + kStatusBin);
  command.AddParameter("--print");
  command.AddParameter("--all_instances");
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  if (int wait_result = command.Start().Wait(); wait_result != 0) {
    WriteAll(err, "      (unknown instance status error)");
  }
}

Result<cvd::Status> InstanceManager::CvdFleetImpl(
    const uid_t uid, const SharedFD& out, const SharedFD& err,
    const std::optional<std::string>& env_config) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  const char _GroupDeviceInfoStart[] = "[\n";
  const char _GroupDeviceInfoSeparate[] = ",\n";
  const char _GroupDeviceInfoEnd[] = "]\n";
  WriteAll(out, _GroupDeviceInfoStart);
  auto&& instance_groups = instance_db.InstanceGroups();

  for (const auto& group : instance_groups) {
    CF_EXPECT(group != nullptr);
    auto config_path = env_config && FileExists(*env_config)
                           ? *env_config
                           : group->GetCuttlefishConfigPath();
    if (config_path.ok()) {
      IssueStatusCommand(out, err, *config_path, *group);
    }
    if (group == *instance_groups.crbegin()) {
      continue;
    }
    WriteAll(out, _GroupDeviceInfoSeparate);
  }
  WriteAll(out, _GroupDeviceInfoEnd);
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

Result<cvd::Status> InstanceManager::CvdFleetHelp(
    const SharedFD& out, const SharedFD& err,
    const std::string& host_tool_dir) {
  Command command(host_tool_dir + kStatusBin);
  command.AddParameter("--help");
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
  if (int wait_result = command.Start().Wait(); wait_result != 0) {
    WriteAll(err, "      (unknown instance status error)");
  }
  WriteAll(out, "\n");
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

Result<cvd::Status> InstanceManager::CvdFleet(
    const uid_t uid, const SharedFD& out, const SharedFD& err,
    const std::optional<std::string>& env_config,
    const std::string& host_tool_dir, const std::vector<std::string>& args) {
  bool is_help = false;
  for (const auto& arg : args) {
    if (arg == "--help" || arg == "-help") {
      is_help = true;
      break;
    }
  }
  return (is_help ? CvdFleetHelp(out, err, host_tool_dir + "/bin/")
                  : CvdFleetImpl(uid, out, err, env_config));
}

void InstanceManager::IssueStopCommand(
    const SharedFD& out, const SharedFD& err,
    const std::string& config_file_path,
    const selector::LocalInstanceGroup& group) {
  Command command(group.HostBinariesDir() + kStopBin);
  command.AddParameter("--clear_instance_dirs");
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  if (int wait_result = command.Start().Wait(); wait_result != 0) {
    WriteAll(err,
             "Warning: error stopping instances for dir \"" + group.HomeDir() +
                 "\".\nThis can happen if instances are already stopped.\n");
  }
  for (const auto& instance : group.Instances()) {
    auto lock = lock_manager_.TryAcquireLock(instance->InstanceId());
    if (lock.ok() && (*lock)) {
      (*lock)->Status(InUseState::kNotInUse);
      continue;
    }
    WriteAll(err, "InstanceLockFileManager failed to acquire lock");
  }
}

cvd::Status InstanceManager::CvdClear(const uid_t uid, const SharedFD& out,
                                      const SharedFD& err) {
  std::lock_guard lock(instance_db_mutex_);
  auto& instance_db = GetInstanceDB(uid);
  cvd::Status status;
  const std::string config_json_name = cpp_basename(GetGlobalConfigFileLink());

  auto&& instance_groups = instance_db.InstanceGroups();
  for (const auto& group : instance_groups) {
    auto config_path = group->GetCuttlefishConfigPath();
    if (config_path.ok()) {
      IssueStopCommand(out, err, *config_path, *group);
    }
    RemoveFile(group->HomeDir() + "/cuttlefish_runtime");
    RemoveFile(group->HomeDir() + config_json_name);
  }
  WriteAll(err, "Stopped all known instances\n");

  instance_db.Clear();
  status.set_code(cvd::Status::OK);
  return status;
}

}  // namespace cuttlefish

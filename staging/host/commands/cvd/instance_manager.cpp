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
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/instance_database_utils.h"
#include "host/commands/cvd/selector_constants.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<std::string> InstanceManager::GetCuttlefishConfigPath(
    const std::string& home) {
  return instance_db::GetCuttlefishConfigPath(home);
}

InstanceManager::InstanceManager(InstanceLockFileManager& lock_manager)
    : lock_manager_(lock_manager) {}

bool InstanceManager::HasInstanceGroups() const {
  std::lock_guard lock(instance_db_mutex_);
  return !instance_db_.IsEmpty();
}

Result<void> InstanceManager::SetInstanceGroup(
    const InstanceManager::InstanceGroupDir& dir,
    const InstanceGroupInfo& info) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  CF_EXPECT(instance_db_.AddInstanceGroup(dir, info.host_binaries_dir));
  auto searched_group =
      CF_EXPECT(instance_db_.FindGroup({selector::kHomeField, dir}));
  for (auto i : info.instances) {
    instance_db_.AddInstance(searched_group, i);
  }
  return {};
}

void InstanceManager::RemoveInstanceGroup(
    const InstanceManager::InstanceGroupDir& dir) {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto result = instance_db_.FindGroup({selector::kHomeField, dir});
  if (!result.ok()) return;
  auto group = *result;
  instance_db_.RemoveInstanceGroup(group);
}

Result<InstanceManager::InstanceGroupInfo>
InstanceManager::GetInstanceGroupInfo(
    const InstanceManager::InstanceGroupDir& dir) const {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  auto group = CF_EXPECT(instance_db_.FindGroup({selector::kHomeField, dir}));
  InstanceGroupInfo info;
  info.host_binaries_dir = group.HostBinariesDir();
  auto instances = group.Instances();
  for (const auto& instance : instances) {
    info.instances.insert(instance.InstanceId());
  }
  return {info};
}

void InstanceManager::IssueStatusCommand(
    const SharedFD& out, const std::string& config_file_path,
    const instance_db::LocalInstanceGroup& group) {
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
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  if (int wait_result = command.Start().Wait(); wait_result != 0) {
    WriteAll(out, "      (unknown instance status error)");
  }
}

cvd::Status InstanceManager::CvdFleetImpl(
    const SharedFD& out, const std::optional<std::string>& env_config) const {
  std::lock_guard assemblies_lock(instance_db_mutex_);
  const char _GroupDeviceInfoStart[] = "[\n";
  const char _GroupDeviceInfoSeparate[] = ",\n";
  const char _GroupDeviceInfoEnd[] = "]\n";
  WriteAll(out, _GroupDeviceInfoStart);
  auto&& instance_groups = instance_db_.InstanceGroups();

  for (const auto& group : instance_groups) {
    auto config_path = env_config && FileExists(*env_config)
                           ? *env_config
                           : group.GetCuttlefishConfigPath();
    if (config_path.ok()) {
      IssueStatusCommand(out, *config_path, group);
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

cvd::Status InstanceManager::CvdFleetHelp(
    const SharedFD& out, const SharedFD& err,
    const std::string& host_tool_dir) const {
  Command command(host_tool_dir + kStatusBin);
  command.AddParameter("--help");
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
  if (int wait_result = command.Start().Wait(); wait_result != 0) {
    WriteAll(out, "      (unknown instance status error)");
  }
  WriteAll(out, "\n");
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

cvd::Status InstanceManager::CvdFleet(
    const SharedFD& out, const SharedFD& err,
    const std::optional<std::string>& env_config,
    const std::string& host_tool_dir,
    const std::vector<std::string>& args) const {
  bool is_help = false;
  for (const auto& arg : args) {
    if (arg == "--help" || arg == "-help") {
      is_help = true;
      break;
    }
  }
  return (is_help ? CvdFleetHelp(out, err, host_tool_dir + "/bin/")
                  : CvdFleetImpl(out, env_config));
}

void InstanceManager::IssueStopCommand(
    const SharedFD& out, const SharedFD& err,
    const std::string& config_file_path,
    const instance_db::LocalInstanceGroup& group) {
  Command command(group.HostBinariesDir() + kStopBin);
  command.AddParameter("--clear_instance_dirs");
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  if (int wait_result = command.Start().Wait(); wait_result != 0) {
    WriteAll(out,
             "Warning: error stopping instances for dir \"" + group.HomeDir() +
                 "\".\nThis can happen if instances are already stopped.\n");
  }
  for (const auto& instance : group.Instances()) {
    auto lock = lock_manager_.TryAcquireLock(instance.InstanceId());
    if (lock.ok() && (*lock)) {
      (*lock)->Status(InUseState::kNotInUse);
      continue;
    }
    WriteAll(err, "InstanceLockFileManager failed to acquire lock");
  }
}

cvd::Status InstanceManager::CvdClear(const SharedFD& out,
                                      const SharedFD& err) {
  std::lock_guard lock(instance_db_mutex_);
  cvd::Status status;
  const std::string config_json_name = cpp_basename(GetGlobalConfigFileLink());

  auto&& instance_groups = instance_db_.InstanceGroups();
  for (const auto& group : instance_groups) {
    auto config_path = group.GetCuttlefishConfigPath();
    if (config_path.ok()) {
      IssueStopCommand(out, err, *config_path, group);
    }
    RemoveFile(group.HomeDir() + "/cuttlefish_runtime");
    RemoveFile(group.HomeDir() + config_json_name);
  }
  WriteAll(out, "Stopped all known instances\n");

  instance_db_.Clear();
  status.set_code(cvd::Status::OK);
  return status;
}

}  // namespace cuttlefish

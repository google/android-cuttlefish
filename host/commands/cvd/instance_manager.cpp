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
  std::lock_guard lock(instance_groups_mutex_);
  return !instance_groups_.empty();
}

void InstanceManager::SetInstanceGroup(
    const InstanceManager::InstanceGroupDir& dir,
    const InstanceManager::InstanceGroupInfo& info) {
  std::lock_guard assemblies_lock(instance_groups_mutex_);
  instance_groups_[dir] = info;
}

void InstanceManager::RemoveInstanceGroup(
    const InstanceManager::InstanceGroupDir& dir) {
  std::lock_guard assemblies_lock(instance_groups_mutex_);
  instance_groups_.erase(dir);
}

Result<InstanceManager::InstanceGroupInfo> InstanceManager::GetInstanceGroup(
    const InstanceManager::InstanceGroupDir& dir) const {
  std::lock_guard assemblies_lock(instance_groups_mutex_);
  auto info_it = instance_groups_.find(dir);
  if (info_it == instance_groups_.end()) {
    return CF_ERR("No group dir \"" << dir << "\"");
  } else {
    return info_it->second;
  }
}

cvd::Status InstanceManager::CvdFleetImpl(
    const SharedFD& out, const std::optional<std::string>& env_config) const {
  std::lock_guard assemblies_lock(instance_groups_mutex_);
  const char _GroupDeviceInfoStart[] = "[\n";
  const char _GroupDeviceInfoSeparate[] = ",\n";
  const char _GroupDeviceInfoEnd[] = "]\n";
  WriteAll(out, _GroupDeviceInfoStart);
  for (const auto& [group_dir, group_info] : instance_groups_) {
    auto config_path = (env_config && FileExists(*env_config))
                           ? *env_config
                           : GetCuttlefishConfigPath(group_dir);
    if (config_path.ok()) {
      // Reads CuttlefishConfig::instance_names(), which must remain stable
      // across changes to config file format (within server_constants.h major
      // version).
      auto config = CuttlefishConfig::GetFromFile(*config_path);
      if (config) {
        Command command(group_info.host_binaries_dir + kStatusBin);
        command.AddParameter("--print");
        command.AddParameter("--all_instances");
        command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
        command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName,
                                       *config_path);
        if (int wait_result = command.Start().Wait(); wait_result != 0) {
          WriteAll(out, "      (unknown instance status error)");
        }
      }
    }
    if (group_dir != instance_groups_.rbegin()->first) {
      WriteAll(out, _GroupDeviceInfoSeparate);
    }
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

cvd::Status InstanceManager::CvdClear(const SharedFD& out,
                                      const SharedFD& err) {
  std::lock_guard lock(instance_groups_mutex_);
  cvd::Status status;
  for (const auto& [group_dir, group_info] : instance_groups_) {
    auto config_path = GetCuttlefishConfigPath(group_dir);
    if (config_path.ok()) {
      // Stop all instances that are using this group dir.
      Command command(group_info.host_binaries_dir + kStopBin);
      // Delete the instance dirs.
      command.AddParameter("--clear_instance_dirs");
      command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
      command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
      command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, *config_path);
      if (int wait_result = command.Start().Wait(); wait_result != 0) {
        WriteAll(
            out,
            "Warning: error stopping instances for dir \"" + group_dir +
                "\".\nThis can happen if instances are already stopped.\n");
      }
      for (const auto& instance : group_info.instances) {
        auto lock = lock_manager_.TryAcquireLock(instance);
        if (lock.ok() && (*lock)) {
          (*lock)->Status(InUseState::kNotInUse);
        }
      }
    }
  }
  RemoveFile(StringFromEnv("HOME", ".") + "/cuttlefish_runtime");
  RemoveFile(GetGlobalConfigFileLink());
  WriteAll(out, "Stopped all known instances\n");

  instance_groups_.clear();
  status.set_code(cvd::Status::OK);
  return status;
}

}  // namespace cuttlefish

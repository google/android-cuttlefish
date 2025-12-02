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

#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"

#include <chrono>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <fmt/format.h>
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/posix/symlink.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/instances/config_path.h"
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"
#include "cuttlefish/host/commands/cvd/instances/instance_record.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/host/commands/cvd/instances/reset_client_utils.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/config_utils.h"

namespace cuttlefish {
namespace {

// Returns true only if command terminated normally, and returns 0
Result<void> RunCommand(Command&& command) {
  auto subprocess = command.Start();
  siginfo_t infop{};
  // This blocks until the process exits, but doesn't reap it.
  auto result = subprocess.Wait(&infop, WEXITED);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  CF_EXPECT(infop.si_code == CLD_EXITED && infop.si_status == 0);
  return {};
}

Result<void> RemoveGroupDirectory(const LocalInstanceGroup& group) {
  std::string per_user_dir = PerUserDir();
  if (!absl::StartsWith(group.HomeDir(), per_user_dir)) {
    LOG(WARNING)
        << "Instance group home directory not under user specific directory("
        << per_user_dir << "), artifacts not deleted";
    return {};
  }
  std::string group_directory = CF_EXPECT(GroupDirFromHome(group.HomeDir()));
  if (DirectoryExists(group_directory)) {
    CF_EXPECT(RecursivelyRemoveDirectory(group_directory),
              "Failed to remove group directory");
  }
  return {};
}

Result<void> LinkOrMakeDir(std::optional<std::string> target,
                           const std::string& path) {
  if (target.has_value()) {
    CF_EXPECT(DirectoryExists(*target));
    CF_EXPECT(Symlink(*target, path));
  } else {
    CF_EXPECTF(EnsureDirectoryExists(path), "Failed to create directory: {}",
               path);
  }
  return {};
}

Result<void> CreateOrLinkGroupDirectories(
    const LocalInstanceGroup& group,
    InstanceManager::GroupDirectories directories) {
  CF_EXPECT(
      LinkOrMakeDir(std::move(directories.base_directory), group.BaseDir()));
  CF_EXPECT(LinkOrMakeDir(std::move(directories.home), group.HomeDir()));
  CF_EXPECT(EnsureDirectoryExists(group.ArtifactsDir()));
  CF_EXPECT(LinkOrMakeDir(std::move(directories.host_artifacts_path),
                          group.HostArtifactsPath()));
  for (size_t i = 0; i < directories.product_out_paths.size(); ++i) {
    CF_EXPECT(LinkOrMakeDir(std::move(directories.product_out_paths[i]),
                            group.ProductDir(i)));
  }
  return {};
}

Result<std::string> StopBin(const std::string& host_android_out) {
  return CF_EXPECT(HostToolTarget(host_android_out).GetStopBinName());
}

Command BuildStopCommand(const std::string& bin,
                         const std::string& config_file_path,
                         std::optional<std::chrono::seconds> launcher_timeout,
                         InstanceDirActionOnStop instance_dir_action) {
  Command cmd(bin);
  if (launcher_timeout.has_value()) {
    cmd.AddParameter("-wait_for_launcher=", launcher_timeout->count());
  } else {
    // zero means wait indefinitely
    cmd.AddParameter("-wait_for_launcher=0");
  }
  if (instance_dir_action == InstanceDirActionOnStop::Clear) {
    cmd.AddParameter("-clear_instance_dirs");
  }
  cmd.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  return cmd;
}

}  // namespace

InstanceManager::InstanceManager(InstanceLockFileManager& lock_manager,
                                 InstanceDatabase& instance_db)
    : lock_manager_(lock_manager), instance_db_(instance_db) {}

Result<std::pair<LocalInstance, LocalInstanceGroup>>
InstanceManager::FindInstanceWithGroup(
    const InstanceDatabase::Filter& filter) const {
  return instance_db_.FindInstanceWithGroup(filter);
}

Result<bool> InstanceManager::HasInstanceGroups() const {
  return !CF_EXPECT(instance_db_.IsEmpty());
}

Result<std::vector<InstanceManager::InternalInstanceDesc>>
InstanceManager::AllocateAndLockInstanceIds(
    std::vector<InstanceParams> instances) {
  std::set<unsigned> requested;
  std::vector<InstanceLockFile> requested_lock_files;
  // Acquire requested locks first
  for (const auto& instance : instances) {
    if (instance.instance_id.has_value()) {
      unsigned id = instance.instance_id.value();
      auto [_, inserted] = requested.insert(id);
      // This check avoids a possible deadlock when trying to acquire the lock a
      // second time
      CF_EXPECTF(std::move(inserted),
                 "Requested instance ids must be distinct, but {} is repeated",
                 id);
      requested_lock_files.emplace_back(
          CF_EXPECT(lock_manager_.AcquireLock(id)));
    }
  }
  std::vector<InternalInstanceDesc> ret;
  ret.reserve(instances.size());
  auto requested_it = requested_lock_files.begin();
  for (auto& instance : instances) {
    if (instance.instance_id.has_value()) {
      CF_EXPECT(requested_it != requested_lock_files.end());
      ret.emplace_back(InternalInstanceDesc{
          .lock_file = std::move(*requested_it),
          .name = std::move(instance.per_instance_name),
      });
      ++requested_it;
    } else {
      ret.emplace_back(InternalInstanceDesc{
          .lock_file = CF_EXPECT(lock_manager_.AcquireUnusedLock()),
          .name = std::move(instance.per_instance_name),
      });
    }
  }
  return ret;
}

Result<LocalInstanceGroup> InstanceManager::CreateInstanceGroup(
    InstanceGroupParams group_params, GroupDirectories directories) {
  CF_EXPECT_EQ(
      group_params.instances.size(), directories.product_out_paths.size(),
      "Number of product directories doesn't match number of instances");

  std::vector<InternalInstanceDesc> instance_descs =
      CF_EXPECT(AllocateAndLockInstanceIds(std::move(group_params.instances)));

  LocalInstanceGroup::Builder group_builder(std::move(group_params.group_name));
  for (const auto& instance_desc : instance_descs) {
    if (instance_desc.name.has_value()) {
      group_builder.AddInstance(instance_desc.lock_file.Instance(),
                                instance_desc.name.value());
    } else {
      group_builder.AddInstance(instance_desc.lock_file.Instance());
    }
  }
  LocalInstanceGroup group = CF_EXPECT(group_builder.Build());

  // The base and other directories are always set to the default location, if
  // the user provides custom directories the ones in the default locations
  // become symbolic links to those.
  CF_EXPECT(CreateOrLinkGroupDirectories(group, std::move(directories)));

  CF_EXPECT(instance_db_.AddInstanceGroup(group));
  for (auto& instance_desc : instance_descs) {
    CF_EXPECT(instance_desc.lock_file.Status(InUseState::kInUse));
  }

  return group;
}

Result<bool> InstanceManager::RemoveInstanceGroup(LocalInstanceGroup group) {
  CF_EXPECT(!group.HasActiveInstances(),
            "Group still contains active instances");
  for (auto& instance : group.Instances()) {
    if (instance.id() == 0) {
      continue;
    }
    auto remove_res = lock_manager_.RemoveLockFile(instance.id());
    if (!remove_res.ok()) {
      LOG(ERROR) << "Failed to remove instance id lock: "
                 << remove_res.error().FormatForEnv();
    }
  }
  CF_EXPECT(RemoveGroupDirectory(group));

  return CF_EXPECT(instance_db_.RemoveInstanceGroup(group.GroupName()));
}

Result<void> InstanceManager::UpdateInstanceGroup(
    const LocalInstanceGroup& group) {
  CF_EXPECT(instance_db_.UpdateInstanceGroup(group));
  return {};
}

Result<void> InstanceManager::IssueStopCommand(
    LocalInstanceGroup& group,
    std::optional<std::chrono::seconds> launcher_timeout,
    InstanceDirActionOnStop instance_dir_action) {
  auto config_file_path = CF_EXPECT(GetCuttlefishConfigPath(group.HomeDir()));
  const auto stop_bin = CF_EXPECT(StopBin(group.HostArtifactsPath()));
  Command command =
      BuildStopCommand(group.HostArtifactsPath() + "/bin/" + stop_bin,
                       config_file_path, launcher_timeout, instance_dir_action);
  auto cmd_result = RunCommand(std::move(command));

  /**
   * --clear_instance_dirs may not be available for old branches. This causes
   * the stop_cvd to terminates with a non-zero exit code due to the parsing
   * error. Then, we will try to re-run it without the flag.
   */
  if (!cmd_result.ok() &&
      instance_dir_action == InstanceDirActionOnStop::Clear) {
    LOG(WARNING)
        << stop_bin << " was executed internally, and failed. It might "
        << "be failing to parse the new --clear_instance_dirs. Will try "
        << "without the flag.\n";
    Command command = BuildStopCommand(
        group.HostArtifactsPath() + "/bin/" + stop_bin, config_file_path,
        launcher_timeout, InstanceDirActionOnStop::Keep);
    cmd_result = RunCommand(std::move(command));
  }

  if (!cmd_result.ok()) {
    LOG(WARNING)
        << "Warning: error stopping instances for dir \"" + group.HomeDir() +
               "\".\nThis can happen if instances are already stopped.\n";
  }
  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  instance_db_.UpdateInstanceGroup(group);
  return {};
}

Result<void> InstanceManager::Clear() {
  const std::string config_json_name =
      android::base::Basename(GetGlobalConfigFileLink());
  auto instance_groups =
      CF_EXPECT(instance_db_.Clear(), "Failed to clear instance database");
  for (auto& group : instance_groups) {
    // Only stop running instances.
    if (group.HasActiveInstances()) {
      auto stop_result = IssueStopCommand(group, std::chrono::seconds(5),
                                          InstanceDirActionOnStop::Clear);
      if (!stop_result.ok()) {
        LOG(ERROR) << stop_result.error().FormatForEnv();
      }
    }
    for (auto instance : group.Instances()) {
      if (instance.id() <= 0) {
        continue;
      }
      auto res = lock_manager_.RemoveLockFile(instance.id());
      if (!res.ok()) {
        LOG(ERROR) << "Failed to remove lock file for instance: "
                   << res.error().FormatForEnv();
      }
    }
    RemoveFile(group.HomeDir() + "/cuttlefish_runtime");
    RemoveFile(group.HomeDir() + config_json_name);
    RemoveGroupDirectory(group);
  }
  std::cerr << "Stopped all known instances\n";
  return {};
}

Result<void> InstanceManager::Reset() {
  CF_EXPECT(Clear());
  CF_EXPECT(KillAllCuttlefishInstances(false));
  return {};
}

Result<void> InstanceManager::ResetAndClearInstanceDirs() {
  CF_EXPECT(Clear());
  CF_EXPECT(KillAllCuttlefishInstances(true));
  return {};
}

Result<std::vector<LocalInstanceGroup>> InstanceManager::FindGroups(
    const InstanceDatabase::Filter& filter) const {
  return instance_db_.FindGroups(filter);
}

Result<LocalInstanceGroup> InstanceManager::FindGroup(
    const InstanceDatabase::Filter& filter) const {
  auto output = CF_EXPECT(instance_db_.FindGroups(filter));
  CF_EXPECT_EQ(output.size(), 1ul);
  return *(output.begin());
}

}  // namespace cuttlefish

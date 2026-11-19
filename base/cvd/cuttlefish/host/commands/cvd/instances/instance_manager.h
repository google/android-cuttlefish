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

#pragma once

#include <sys/types.h>

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct InstanceParams {
  std::optional<unsigned> instance_id;
  std::optional<std::string> per_instance_name;
};

struct InstanceGroupParams {
  std::string group_name;
  std::vector<InstanceParams> instances;
};

enum class InstanceDirActionOnStop {
  Keep,
  Clear,
};

class InstanceManager {
 public:
  struct GroupDirectories {
    std::optional<std::string> base_directory;
    std::optional<std::string> home;
    std::optional<std::string> host_artifacts_path;
    std::vector<std::optional<std::string>> product_out_paths;
  };
  // `lock_manager` may be null in sandboxed or non-Linux environments where
  // host-resources lock files are not used.
  InstanceManager(InstanceLockFileManager* lock_manager,
                  InstanceDatabase& instance_db);
  InstanceManager(InstanceLockFileManager* lock_manager,
                  InstanceDatabase& instance_db, std::string base_root_dir);

  // Acquires an exclusive non-blocking flock on `<BaseDir>/in_progress.lock`
  // to guard an in-progress prepare or boot transition against concurrent
  // state refreshes and duplicate starts.
  static Result<Fd> AcquireGroupInProgressLock(const LocalInstanceGroup& group);
  // Returns true if another open file description currently holds an exclusive
  // flock on `<BaseDir>/in_progress.lock`.
  static bool IsGroupTransitionInProgress(const LocalInstanceGroup& group);

  Result<bool> HasInstanceGroups() const;
  // Creates and registers an instance group. If `out_in_progress_lock` is
  // non-null, acquires `in_progress.lock` before inserting the group into
  // `instance_db_` so no unlocked `PREPARING`/`STOPPED` window exists.
  Result<LocalInstanceGroup> CreateInstanceGroup(
      InstanceGroupParams group_params, GroupDirectories group_directories,
      std::optional<Fd>* out_in_progress_lock = nullptr);
  Result<void> UpdateInstanceGroup(const LocalInstanceGroup& group);
  Result<bool> RemoveInstanceGroup(LocalInstanceGroup group);
  // Acquires all instance ID lock files for `group` and marks them
  // `InUseState::kInUse`.
  Result<void> EnsureInstanceLocksInUse(const LocalInstanceGroup& group);
  // Reconciles `group`'s persisted states against live launcher monitor sockets
  // and `run_cvd` processes. Set `caller_holds_in_progress_lock` to true when
  // the caller already holds `AcquireGroupInProgressLock(group)`.
  Result<LocalInstanceGroup> RefreshInstanceGroupState(
      LocalInstanceGroup group, bool caller_holds_in_progress_lock = false);
  Result<void> RefreshInstanceGroups();
  // Refreshes all groups and removes inactive groups whose backing directories
  // (`HomeDir`, `HostArtifactsPath`, or `ProductOutPath`) no longer exist.
  Result<void> PruneBrokenInstanceGroups();

  // Stops and removes all known instance instance groups
  Result<void> Clear();
  // Similar to Clear(), but also attempts to stop devices owned by the current
  // user and not tracked in the instance database.
  Result<void> Reset();
  Result<void> ResetAndClearInstanceDirs();

  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const InstanceDatabase::Filter& filter) const;
  Result<LocalInstanceGroup> FindGroup(
      const InstanceDatabase::Filter& filter) const;

  Result<std::pair<LocalInstance, LocalInstanceGroup>> FindInstanceWithGroup(
      const InstanceDatabase::Filter& filter) const;
  Result<std::vector<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>>
  FindInstances(const InstanceDatabase::Filter& filter) const;

  // Stops the device by asking it over the control socket. If launcher_timeout
  // has a value, it will wait for at most that time before returning an error.
  Result<void> StopInstanceGroup(
      LocalInstanceGroup& group, InstanceDirActionOnStop instance_dir_action,
      std::chrono::seconds launcher_timeout = std::chrono::seconds(5),
      const std::vector<unsigned>& instance_nums = {});

  Result<void> ForcefullyStopGroup(unsigned any_id_in_group);

 private:
  struct InternalInstanceDesc {
    unsigned id;
    std::optional<InstanceLockFile> lock_file;
    std::optional<std::string> name;
  };

  Result<std::vector<InternalInstanceDesc>> AllocateAndLockInstanceIds(
      std::vector<InstanceParams> instances);

  InstanceLockFileManager* lock_manager_;
  InstanceDatabase& instance_db_;
  std::string base_root_dir_;
};

}  // namespace cuttlefish

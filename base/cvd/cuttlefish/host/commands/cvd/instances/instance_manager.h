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

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"

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
  InstanceManager(InstanceLockFileManager&, InstanceDatabase& instance_db);

  Result<bool> HasInstanceGroups() const;
  Result<LocalInstanceGroup> CreateInstanceGroup(
      InstanceGroupParams group_params, GroupDirectories group_directories);
  Result<void> UpdateInstanceGroup(const LocalInstanceGroup& group);
  Result<bool> RemoveInstanceGroup(LocalInstanceGroup group);

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

  // Stops the device by asking it over the control socket. If launcher_timeout
  // has a value, it will wait for at most that time before returning an error.
  Result<void> StopInstanceGroup(
      LocalInstanceGroup& group,
      std::optional<std::chrono::seconds> launcher_timeout,
      InstanceDirActionOnStop instance_dir_action);

 private:
  struct InternalInstanceDesc {
    InstanceLockFile lock_file;
    std::optional<std::string> name;
  };

  Result<std::vector<InternalInstanceDesc>> AllocateAndLockInstanceIds(
      std::vector<InstanceParams> instances);

  InstanceLockFileManager& lock_manager_;
  InstanceDatabase& instance_db_;
};

}  // namespace cuttlefish

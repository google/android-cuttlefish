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

#include "host/commands/cvd/instance_database.h"

#include <algorithm>

#include <android-base/parseint.h>

#include "common/libs/utils/files.h"
#include "host/commands/cvd/instance_database_utils.h"
#include "host/commands/cvd/selector_constants.h"

namespace cuttlefish {
namespace instance_db {

void InstanceDatabase::Clear() { local_instance_groups_.clear(); }

Result<void> InstanceDatabase::AddInstanceGroup(
    const std::string& home_dir, const std::string& host_binaries_dir) {
  CF_EXPECT(EnsureDirectoryExists(home_dir),
            "HOME dir, " << home_dir << " does not exist");
  CF_EXPECT(PotentiallyHostBinariesDir(host_binaries_dir),
            "ANDROID_HOST_OUT, " << host_binaries_dir << " is not a tool dir");

  Query query = {selector::kHomeField, home_dir};
  auto instance_groups =
      CF_EXPECT(Find<LocalInstanceGroup>(query, group_handlers_));
  if (!instance_groups.empty()) {
    return CF_ERR(home_dir << " is already taken");
  }
  local_instance_groups_.emplace_back(home_dir, host_binaries_dir);
  return {};
}

Result<void> InstanceDatabase::AddInstance(const LocalInstanceGroup& group,
                                           const unsigned id,
                                           const std::string& instance_name) {
  CF_EXPECT(IsValidInstanceName(instance_name),
            "instance_name " << instance_name << " is invalid.");
  auto itr = std::find(local_instance_groups_.begin(),
                       local_instance_groups_.end(), group);
  CF_EXPECT(
      itr != local_instance_groups_.end(),
      "Adding instances to non-existing group " + group.InternalGroupName());

  auto instances = CF_EXPECT(
      FindInstances({selector::kInstanceIdField, std::to_string(id)}));
  if (instances.size() != 0) {
    return CF_ERR("instance id " << id << " is taken");
  }

  auto instances_by_name = CF_EXPECT(group.FindByInstanceName(instance_name));
  if (!instances_by_name.empty()) {
    return CF_ERR("instance name " << instance_name << " is taken");
  }
  return itr->AddInstance(id, instance_name);
}

bool InstanceDatabase::RemoveInstanceGroup(const LocalInstanceGroup& group) {
  auto itr = std::find(local_instance_groups_.begin(),
                       local_instance_groups_.end(), group);
  if (itr == local_instance_groups_.end()) {
    return false;
  }
  local_instance_groups_.erase(itr);
  return true;
}

Result<Set<LocalInstanceGroup>> InstanceDatabase::FindGroupsByHome(
    const std::string& home) const {
  auto subset = CollectToSet<LocalInstanceGroup>(
      local_instance_groups_, [&home](const LocalInstanceGroup& group) {
        return group.HomeDir() == home;
      });
  return AtMostOne(subset, TooManyInstancesFound(1, selector::kHomeField));
}

Result<Set<LocalInstance>> InstanceDatabase::FindInstancesById(
    const std::string& id) const {
  int parsed_int = 0;
  if (!android::base::ParseInt(id, &parsed_int)) {
    return CF_ERR(id << " cannot be converted to an integer");
  }
  auto subset = CollectAllElements<LocalInstance, LocalInstanceGroup>(
      [parsed_int](const LocalInstanceGroup& group) {
        return group.FindById(parsed_int);
      },
      local_instance_groups_);
  CF_EXPECT(subset.ok());
  return AtMostOne(*subset,
                   TooManyInstancesFound(1, selector::kInstanceIdField));
}

Result<Set<LocalInstance>> InstanceDatabase::FindInstancesByInstanceName(
    const Value& instance_specific_name) const {
  auto subset = CollectAllElements<LocalInstance, LocalInstanceGroup>(
      [&instance_specific_name](const LocalInstanceGroup& group) {
        return group.FindByInstanceName(instance_specific_name);
      },
      local_instance_groups_);
  CF_EXPECT(subset.ok());
  return AtMostOne(*subset,
                   TooManyInstancesFound(1, selector::kInstanceNameField));
}

}  // namespace instance_db
}  // namespace cuttlefish

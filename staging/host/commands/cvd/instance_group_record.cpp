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

#include "host/commands/cvd/instance_group_record.h"

#include "host/commands/cvd/instance_database_utils.h"
#include "host/commands/cvd/selector_constants.h"

namespace cuttlefish {
namespace instance_db {

LocalInstanceGroup::LocalInstanceGroup(const std::string& home_dir,
                                       const std::string& host_binaries_dir)
    : home_dir_{home_dir},
      host_binaries_dir_{host_binaries_dir},
      internal_group_name_(GenInternalGroupName()) {}

Result<std::string> LocalInstanceGroup::GetCuttlefishConfigPath() const {
  return cuttlefish::instance_db::GetCuttlefishConfigPath(HomeDir());
}

std::size_t LocalInstanceGroup::HashCode() const noexcept {
  auto hash_function = std::hash<decltype(home_dir_)>();
  return hash_function(home_dir_);
}

Result<void> LocalInstanceGroup::AddInstance(const int instance_id) {
  if (HasInstance(instance_id)) {
    return CF_ERR("Instance Id " << instance_id << " is taken");
  }
  instances_.emplace(instance_id, internal_group_name_);
  return {};
}

Result<void> LocalInstanceGroup::AddInstance(const LocalInstance& instance) {
  return AddInstance(instance.InstanceId());
}

Result<Set<LocalInstance>> LocalInstanceGroup::FindById(const int id) const {
  auto subset = CollectToSet<LocalInstance>(
      instances_, [id](const LocalInstance& instance) {
        return instance.InstanceId() == id;
      });
  return AtMostOne(subset,
                   TooManyInstancesFound(1, selector::kInstanceIdField));
}

bool LocalInstanceGroup::HasInstance(const int instance_id) const {
  for (const auto& instance : instances_) {
    if (instance_id == instance.InstanceId()) {
      return true;
    }
  }
  return false;
}

}  // namespace instance_db
}  // namespace cuttlefish

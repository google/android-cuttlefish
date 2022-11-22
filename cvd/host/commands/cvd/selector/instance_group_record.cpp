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

#include "host/commands/cvd/selector/instance_group_record.h"

#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

LocalInstanceGroup::LocalInstanceGroup(const std::string& group_name,
                                       const std::string& home_dir,
                                       const std::string& host_artifacts_path)
    : home_dir_{home_dir},
      host_artifacts_path_{host_artifacts_path},
      internal_group_name_(GenInternalGroupName()),
      group_name_(group_name) {}

Result<std::string> LocalInstanceGroup::GetCuttlefishConfigPath() const {
  return ::cuttlefish::selector::GetCuttlefishConfigPath(HomeDir());
}

Result<void> LocalInstanceGroup::AddInstance(const unsigned instance_id,
                                             const std::string& instance_name) {
  if (HasInstance(instance_id)) {
    return CF_ERR("Instance Id " << instance_id << " is taken");
  }
  LocalInstance* instance =
      new LocalInstance(*this, instance_id, instance_name);
  instances_.emplace(std::unique_ptr<LocalInstance>(instance));
  return {};
}

Result<Set<ConstRef<LocalInstance>>> LocalInstanceGroup::FindById(
    const unsigned id) const {
  auto subset = CollectToSet<LocalInstance>(
      instances_, [&id](const std::unique_ptr<LocalInstance>& instance) {
        return instance && (instance->InstanceId() == id);
      });
  return AtMostOne(subset,
                   GenerateTooManyInstancesErrorMsg(1, kInstanceIdField));
}

Result<Set<ConstRef<LocalInstance>>> LocalInstanceGroup::FindByInstanceName(
    const std::string& instance_name) const {
  auto subset = CollectToSet<LocalInstance>(
      instances_,
      [&instance_name](const std::unique_ptr<LocalInstance>& instance) {
        return instance && (instance->PerInstanceName() == instance_name);
      });

  // note that inside a group, the instance name is unique. However,
  // across groups, they can be multiple
  return AtMostOne(subset,
                   GenerateTooManyInstancesErrorMsg(1, kInstanceNameField));
}

bool LocalInstanceGroup::HasInstance(const unsigned instance_id) const {
  for (const auto& instance : instances_) {
    if (!instance) {
      continue;
    }
    if (instance_id == instance->InstanceId()) {
      return true;
    }
  }
  return false;
}

}  // namespace selector
}  // namespace cuttlefish

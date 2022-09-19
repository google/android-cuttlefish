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

#include <android-base/parseint.h>

#include "host/commands/cvd/instance_database_utils.h"
#include "host/commands/cvd/selector_constants.h"

namespace cuttlefish {
namespace instance_db {

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
  auto all_elements = CollectAllElements<LocalInstance, LocalInstanceGroup>(
      [](const LocalInstanceGroup& group) { return group.Instances(); },
      local_instance_groups_);

  int parsed_int = 0;
  if (!android::base::ParseInt(id, &parsed_int)) {
    return CF_ERR(id << " cannot be converted to an integer");
  }
  auto subset = CollectToSet<LocalInstance>(
      all_elements, [parsed_int](const LocalInstance& instance) {
        return instance.InstanceId() == parsed_int;
      });
  return AtMostOne(subset,
                   TooManyInstancesFound(1, selector::kInstanceIdField));
}

}  // namespace instance_db
}  // namespace cuttlefish

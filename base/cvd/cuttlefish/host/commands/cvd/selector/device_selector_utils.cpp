/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/selector/device_selector_utils.h"

#include "common/libs/utils/users.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

Result<LocalInstanceGroup> GetDefaultGroup(
    const InstanceDatabase& instance_database, const uid_t client_uid) {
  const auto& all_groups = instance_database.InstanceGroups();
  if (all_groups.size() == 1) {
    return *(all_groups.front());
  }
  std::string system_wide_home = CF_EXPECT(SystemWideUserHome(client_uid));
  auto group =
      CF_EXPECT(instance_database.FindGroup({kHomeField, system_wide_home}));
  return group;
}

}  // namespace selector
}  // namespace cuttlefish

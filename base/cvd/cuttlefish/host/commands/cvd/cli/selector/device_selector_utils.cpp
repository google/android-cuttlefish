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

#include "host/commands/cvd/cli/selector/device_selector_utils.h"

#include <android-base/parseint.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/cli/selector/selector_constants.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace selector {

Result<LocalInstanceGroup> GetDefaultGroup(
    const InstanceDatabase& instance_database) {
  const auto all_groups = CF_EXPECT(instance_database.InstanceGroups());
  if (all_groups.size() == 1) {
    return all_groups.front();
  }
  std::string system_wide_home = CF_EXPECT(SystemWideUserHome());
  auto group =
      CF_EXPECT(instance_database.FindGroup({kHomeField, system_wide_home}));
  return group;
}

std::optional<std::string> OverridenHomeDirectory(const cvd_common::Envs& env) {
  Result<std::string> user_home_res = SystemWideUserHome();
  auto home_it = env.find("HOME");
  if (!user_home_res.ok() || home_it == env.end() ||
      home_it->second == *user_home_res) {
    return std::nullopt;
  }
  return home_it->second;
}

Result<Queries> BuildQueriesFromSelectors(const SelectorOptions& selectors,
                                          const cvd_common::Envs& env) {
  selector::Queries queries;
  auto overriden_home = OverridenHomeDirectory(env);
  if (overriden_home.has_value()) {
    queries.emplace_back(kHomeField, overriden_home.value());
  }
  if (selectors.group_name) {
    queries.emplace_back(kGroupNameField, selectors.group_name.value());
  }
  if (selectors.instance_names) {
    const auto per_instance_names = selectors.instance_names.value();
    for (const auto& per_instance_name : per_instance_names) {
      queries.emplace_back(kInstanceNameField, per_instance_name);
    }
  }
  auto it = env.find(kCuttlefishInstanceEnvVarName);
  if (it != env.end()) {
    int id;
    auto cuttlefish_instance = it->second;
    CF_EXPECT(android::base::ParseInt(cuttlefish_instance, &id));
    queries.emplace_back(kInstanceIdField, cuttlefish_instance);
  }

  return queries;
}

}  // namespace selector
}  // namespace cuttlefish

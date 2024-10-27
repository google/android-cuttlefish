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

#include "host/commands/cvd/instances/instance_selector.h"

#include <utility>

#include <android-base/parseint.h>

#include "host/commands/cvd/cli/selector/device_selector_utils.h"
#include "host/commands/cvd/cli/selector/selector_common_parser.h"
#include "host/commands/cvd/instances/instance_database_types.h"

namespace cuttlefish {

Result<InstanceSelector> InstanceSelector::GetSelector(
    const selector::SelectorOptions& selector_options,
    const Queries& extra_queries, const cvd_common::Envs& envs) {
  Queries queries =
      CF_EXPECT(BuildQueriesFromSelectors(selector_options, envs));

  for (const auto& extra_query : extra_queries) {
    queries.push_back(extra_query);
  }

  InstanceSelector instance_selector(queries);
  return instance_selector;
}

Result<std::pair<LocalInstance, LocalInstanceGroup>>
InstanceSelector::FindInstanceWithGroup(
    const InstanceDatabase& instance_database) {
  if (queries_.empty()) {
    return CF_EXPECT(FindDefaultInstance(instance_database));
  }

  return CF_EXPECT(instance_database.FindInstanceWithGroup(queries_));
}

Result<std::pair<LocalInstance, LocalInstanceGroup>>
InstanceSelector::FindDefaultInstance(
    const InstanceDatabase& instance_database) {
  auto group = CF_EXPECT(selector::GetDefaultGroup(instance_database));
  const auto instances = group.Instances();
  CF_EXPECT_EQ(instances.size(), 1u,
               "Default instance is the single instance in the default group.");
  return std::make_pair(*instances.cbegin(), group);
}

}  // namespace cuttlefish

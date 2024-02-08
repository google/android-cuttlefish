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

#include "host/commands/cvd/selector/group_selector.h"

#include <android-base/parseint.h>

#include "common/libs/utils/contains.h"
#include "host/commands/cvd/selector/device_selector_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace selector {

Result<GroupSelector> GroupSelector::GetSelector(
    const cvd_common::Args& selector_args, const Queries& extra_queries,
    const cvd_common::Envs& envs) {
  cvd_common::Args selector_args_copied{selector_args};
  SelectorCommonParser common_parser =
      CF_EXPECT(SelectorCommonParser::Parse(selector_args_copied, envs));
  std::stringstream unused_args;
  unused_args << "{";
  for (const auto& arg : selector_args_copied) {
    unused_args << arg << ", ";
  }
  std::string unused_arg_list = unused_args.str();
  if (!selector_args_copied.empty()) {
    unused_arg_list.pop_back();
    unused_arg_list.pop_back();
  }
  unused_arg_list.append("}");
  if (!selector_args_copied.empty()) {
    LOG(ERROR) << "Warning: there are unused selector options. "
               << unused_arg_list;
  }

  // search by group and instances
  // search by HOME if overridden
  Queries queries;
  if (IsHomeOverridden(common_parser)) {
    CF_EXPECT(common_parser.Home());
    queries.emplace_back(kHomeField, common_parser.Home().value());
  }
  if (common_parser.GroupName()) {
    queries.emplace_back(kGroupNameField, common_parser.GroupName().value());
  }
  if (common_parser.PerInstanceNames()) {
    const auto per_instance_names = common_parser.PerInstanceNames().value();
    for (const auto& per_instance_name : per_instance_names) {
      queries.emplace_back(kInstanceNameField, per_instance_name);
    }
  }
  // if CUTTLEFISH_INSTANCE is set, cvd start should ignore if there's
  // --base_instance_num, etc. cvd start has its own custom logic. Thus,
  // non-start operations cannot share the SelectorCommonParser to parse
  // the environment variable. It should be here.
  if (Contains(envs, kCuttlefishInstanceEnvVarName)) {
    int id;
    auto cuttlefish_instance = envs.at(kCuttlefishInstanceEnvVarName);
    CF_EXPECT(android::base::ParseInt(cuttlefish_instance, &id));
    queries.emplace_back(kInstanceIdField, cuttlefish_instance);
  }

  for (const auto& extra_query : extra_queries) {
    queries.push_back(extra_query);
  }

  GroupSelector group_selector(queries);
  return group_selector;
}

bool GroupSelector::IsHomeOverridden(
    const SelectorCommonParser& common_parser) {
  auto home_overridden_result = common_parser.HomeOverridden();
  if (!home_overridden_result.ok()) {
    return false;
  }
  return *home_overridden_result;
}

Result<LocalInstanceGroup> GroupSelector::FindGroup(
    const InstanceDatabase& instance_database) {
  if (queries_.empty()) {
    auto default_group = CF_EXPECT(FindDefaultGroup(instance_database));
    return default_group;
  }
  auto groups = CF_EXPECT(instance_database.FindGroups(queries_));
  CF_EXPECT(groups.size() == 1, "groups.size() = " << groups.size());
  return *(groups.cbegin());
}

Result<LocalInstanceGroup> GroupSelector::FindDefaultGroup(
    const InstanceDatabase& instance_database) {
  auto group = CF_EXPECT(GetDefaultGroup(instance_database));
  return group;
}

}  // namespace selector
}  // namespace cuttlefish

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
#include "host/commands/cvd/selector/device_selector_utils.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

Result<LocalInstanceGroup> GroupSelector::Select(
    const cvd_common::Args& selector_args, const uid_t uid,
    const InstanceDatabase& instance_database, const cvd_common::Envs& envs) {
  cvd_common::Args selector_args_copied{selector_args};
  SelectorCommonParser common_parser =
      CF_EXPECT(SelectorCommonParser::Parse(uid, selector_args_copied, envs));
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
  GroupSelector group_selector(uid, std::move(common_parser), envs,
                               instance_database);
  auto group = CF_EXPECT(group_selector.FindGroup());
  return group;
}

bool GroupSelector::IsHomeOverridden() const {
  auto home_overridden_result = common_parser_.HomeOverridden();
  if (!home_overridden_result.ok()) {
    return false;
  }
  return *home_overridden_result;
}

bool GroupSelector::RequestsDefaultGroup() const {
  return !common_parser_.HasDeviceSelectOption() && !IsHomeOverridden();
}

Result<LocalInstanceGroup> GroupSelector::FindGroup() {
  if (RequestsDefaultGroup()) {
    auto default_group = CF_EXPECT(FindDefaultGroup());
    return default_group;
  }
  // search by group and instances
  // search by HOME if overridden
  Queries queries;
  if (IsHomeOverridden()) {
    CF_EXPECT(common_parser_.Home());
    queries.emplace_back(kHomeField, common_parser_.Home().value());
  }
  if (common_parser_.GroupName()) {
    queries.emplace_back(kGroupNameField, common_parser_.GroupName().value());
  }
  if (common_parser_.PerInstanceNames()) {
    const auto per_instance_names = common_parser_.PerInstanceNames().value();
    for (const auto& per_instance_name : per_instance_names) {
      queries.emplace_back(kInstanceNameField, per_instance_name);
    }
  }
  CF_EXPECT(!queries.empty());
  auto groups = CF_EXPECT(instance_database_.FindGroups(queries));
  CF_EXPECT(groups.size() == 1, "groups.size() = " << groups.size());
  return *(groups.cbegin());
}

Result<LocalInstanceGroup> GroupSelector::FindDefaultGroup() {
  auto group = CF_EXPECT(GetDefaultGroup(instance_database_, client_uid_));
  return group;
}

}  // namespace selector
}  // namespace cuttlefish

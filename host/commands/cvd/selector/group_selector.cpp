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
  CF_EXPECT(selector_args_copied.empty(),
            "Unused selector options : " << unused_arg_list);
  GroupSelector group_selector(uid, std::move(common_parser),
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
  auto home_overridden_result = common_parser_.HomeOverridden();
  return !common_parser_.HasDeviceSelectOption() && !IsHomeOverridden();
}

Result<LocalInstanceGroup> GroupSelector::FindGroup() {
  if (RequestsDefaultGroup()) {
    auto default_group = CF_EXPECT(FindDefaultGroup());
    return default_group;
  }
  // search by group and instances
  // search by HOME if overridden
  return CF_ERR("Unsupported. Use HOME=<home dir> cvd stop");
}

Result<LocalInstanceGroup> GroupSelector::FindDefaultGroup() {
  const auto& all_groups = instance_database_.InstanceGroups();
  if (all_groups.size() == 1) {
    return *(all_groups.front());
  }
  std::string system_wide_home = CF_EXPECT(SystemWideUserHome(client_uid_));
  auto group =
      CF_EXPECT(instance_database_.FindGroup({kHomeField, system_wide_home}));
  return group;
}

}  // namespace selector
}  // namespace cuttlefish

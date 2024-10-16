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

#include "host/commands/cvd/selector/selector_common_parser.h"

#include <unistd.h>

#include <string>
#include <unordered_set>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

std::vector<std::string> SelectorOptions::AsArgs() const {
  std::vector<std::string> ret;
  if (group_name) {
    ret.push_back(
        fmt::format("--{}={}", SelectorFlags::kGroupName, *group_name));
  }
  if (instance_names) {
    ret.push_back(fmt::format("--{}={}", SelectorFlags::kInstanceName,
                              android::base::Join(*instance_names, ",")));
  }
  return ret;
}

Result<std::string> HandleGroupName(const std::string& group_name) {
  CF_EXPECTF(IsValidGroupName(group_name), "Invalid group name: {}",
             group_name);
  return group_name;
}

Result<std::vector<std::string>> HandleInstanceNames(
    const std::string& per_instance_names) {
  auto instance_names = android::base::Split(per_instance_names, ",");
  std::unordered_set<std::string> duplication_check;
  for (const auto& instance_name : instance_names) {
    CF_EXPECT(IsValidInstanceName(instance_name));
    // Check that provided non-empty instance names are unique. Empty names will
    // be replaced later with defaults guaranteed to be unique.
    CF_EXPECT(instance_name.empty() ||
              !Contains(duplication_check, instance_name));
    duplication_check.insert(instance_name);
  }
  return instance_names;
}

Result<SelectorOptions> HandleNameOpts(
    const std::optional<std::string>& group_name,
    const std::optional<std::string>& instance_names) {
  SelectorOptions ret;
  if (group_name) {
    ret.group_name = CF_EXPECT(HandleGroupName(*group_name));
  }

  if (instance_names) {
    ret.instance_names = CF_EXPECT(HandleInstanceNames(*instance_names));
  }
  return ret;
}

Result<SelectorOptions> ParseCommonSelectorArguments(
    cvd_common::Args& args) {
  // Handling name-related options
  auto group_name_flag =
      CF_EXPECT(SelectorFlags::Get().GetFlag(SelectorFlags::kGroupName));
  auto instance_name_flag =
      CF_EXPECT(SelectorFlags::Get().GetFlag(SelectorFlags::kInstanceName));
  std::optional<std::string> group_name_opt =
      CF_EXPECT(group_name_flag.FilterFlag<std::string>(args));
  std::optional<std::string> instance_name_opt =
      CF_EXPECT(instance_name_flag.FilterFlag<std::string>(args));

  return CF_EXPECT(HandleNameOpts(group_name_opt, instance_name_opt));
}

}  // namespace selector
}  // namespace cuttlefish

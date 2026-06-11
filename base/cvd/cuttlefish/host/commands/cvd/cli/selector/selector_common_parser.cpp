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

#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"

#include <unistd.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_constants.h"
#include "cuttlefish/host/commands/cvd/instances/device_name.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace selector {
namespace {
Result<void> ValidateGroupNameOpt(
    const std::optional<std::string>& group_name) {
  if (!group_name) {
    return {};
  }
  CF_EXPECTF(IsValidGroupName(*group_name), "Invalid group name: {}",
             *group_name);
  return {};
}

Result<void> ValidateInstanceNamesOpt(
    const std::optional<std::vector<std::string>>& instance_names) {
  if (!instance_names) {
    return {};
  }
  std::unordered_set<std::string_view> duplication_check;
  for (const auto& instance_name : *instance_names) {
    CF_EXPECT(IsValidInstanceName(instance_name));
    // Check that provided non-empty instance names are unique. Empty names will
    // be replaced later with defaults guaranteed to be unique.
    CF_EXPECT(instance_name.empty() ||
              !Contains(duplication_check, instance_name));
    duplication_check.emplace(instance_name);
  }
  return {};
}

}  // namespace

std::vector<Flag> BuildCommonSelectorFlags(SelectorOptions& opts) {
  Flag group_name_flag =
      GflagsCompatFlag(SelectorFlags::kGroupName, opts.group_name)
          .AddValidator([&opts]() -> Result<void> {
            CF_EXPECT(ValidateGroupNameOpt(opts.group_name));
            return {};
          })
          .Help(
              "Instance group name. If only one group exists it will default "
              "to that when no group name is provided.");
  Flag instance_name_flag =
      GflagsCompatFlag(SelectorFlags::kInstanceName, opts.instance_names)
          .Alias(SelectorFlags::kInstanceNames)
          .AddValidator([&opts]() -> Result<void> {
            CF_EXPECT(ValidateInstanceNamesOpt(opts.instance_names));
            return {};
          })
          .Help(
              "Comma separated list of instance names. If a single group with "
              "a single instance exists it will default to that when not "
              "provided.");
  return {group_name_flag, instance_name_flag};
}

}  // namespace selector
}  // namespace cuttlefish

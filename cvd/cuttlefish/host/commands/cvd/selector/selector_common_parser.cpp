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

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/selector/selector_option_parser_utils.h"

namespace cuttlefish {
namespace selector {

Result<SelectorCommonParser> SelectorCommonParser::Parse(
    const uid_t client_uid, cvd_common::Args& selector_args,
    const cvd_common::Envs& envs) {
  std::string system_wide_home = CF_EXPECT(SystemWideUserHome(client_uid));
  SelectorCommonParser parser(system_wide_home, selector_args, envs);
  CF_EXPECT(parser.ParseOptions());
  return std::move(parser);
}

SelectorCommonParser::SelectorCommonParser(const std::string& client_user_home,
                                           cvd_common::Args& selector_args,
                                           const cvd_common::Envs& envs)
    : client_user_home_(client_user_home),
      selector_args_{std::addressof(selector_args)},
      envs_{std::addressof(envs)} {}

Result<bool> SelectorCommonParser::HomeOverridden() const {
  return Contains(*envs_, "HOME") && (client_user_home_ != envs_->at("HOME"));
}

Result<void> SelectorCommonParser::ParseOptions() {
  // Handling name-related options
  std::optional<std::string> group_name;
  std::optional<std::string> instance_name;

  std::unordered_map<std::string, std::optional<std::string>> key_optional_map =
      {
          {kGroupNameOpt, std::optional<std::string>{}},
          {kInstanceNameOpt, std::optional<std::string>{}},
      };

  for (auto& [flag_name, value] : key_optional_map) {
    // value is set to std::nullopt if parsing failed or no flag_name flag is
    // given.
    CF_EXPECT(FilterSelectorFlag(*selector_args_, flag_name, value));
  }

  NameFlagsParam name_flags_param{
      .group_name = key_optional_map[kGroupNameOpt],
      .instance_names = key_optional_map[kInstanceNameOpt]};
  auto parsed_name_flags = CF_EXPECT(HandleNameOpts(name_flags_param));
  group_name_ = parsed_name_flags.group_name;
  instance_names_ = parsed_name_flags.instance_names;
  return {};
}

Result<SelectorCommonParser::ParsedNameFlags>
SelectorCommonParser::HandleNameOpts(const NameFlagsParam& name_flags) const {
  std::optional<std::string> group_name_output;
  std::optional<std::vector<std::string>> instance_names_output;
  if (name_flags.group_name) {
    group_name_output = CF_EXPECT(HandleGroupName(name_flags.group_name));
  }

  if (name_flags.instance_names) {
    instance_names_output =
        std::move(CF_EXPECT(HandleInstanceNames(name_flags.instance_names)));
  }
  return {ParsedNameFlags{.group_name = std::move(group_name_output),
                          .instance_names = std::move(instance_names_output)}};
}

Result<std::vector<std::string>> SelectorCommonParser::HandleInstanceNames(
    const std::optional<std::string>& per_instance_names) const {
  CF_EXPECT(per_instance_names && !per_instance_names.value().empty());

  auto instance_names =
      CF_EXPECT(SeparateButWithNoEmptyToken(per_instance_names.value(), ","));
  for (const auto& instance_name : instance_names) {
    CF_EXPECT(IsValidInstanceName(instance_name));
  }
  std::unordered_set<std::string> duplication_check{instance_names.cbegin(),
                                                    instance_names.cend()};
  CF_EXPECT(duplication_check.size() == instance_names.size());
  return instance_names;
}

Result<std::string> SelectorCommonParser::HandleGroupName(
    const std::optional<std::string>& group_name) const {
  CF_EXPECT(group_name && !group_name.value().empty());
  CF_EXPECT(IsValidGroupName(group_name.value()), group_name.value()
                                                      << " failed");
  return {group_name.value()};
}

}  // namespace selector
}  // namespace cuttlefish

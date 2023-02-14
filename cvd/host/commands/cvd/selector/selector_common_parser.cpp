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
  SelectorCommonParser parser(system_wide_home, envs);
  CF_EXPECT(parser.ParseOptions(selector_args));
  return std::move(parser);
}

SelectorCommonParser::SelectorCommonParser(const std::string& client_user_home,
                                           const cvd_common::Envs& envs)
    : client_user_home_(client_user_home), envs_{envs} {}

Result<bool> SelectorCommonParser::HomeOverridden() const {
  return Contains(envs_, "HOME") && (client_user_home_ != envs_.at("HOME"));
}

std::optional<std::string> SelectorCommonParser::Home() const {
  if (Contains(envs_, "HOME")) {
    return envs_.at("HOME");
  }
  return std::nullopt;
}

Result<void> SelectorCommonParser::ParseOptions(
    cvd_common::Args& selector_args) {
  // Handling name-related options
  auto group_name_flag = CF_EXPECT(
      SelectorFlags::Get().GetFlag<std::string>(SelectorFlags::kGroupName));
  auto instance_name_flag = CF_EXPECT(
      SelectorFlags::Get().GetFlag<std::string>(SelectorFlags::kInstanceName));
  auto group_name_opt = CF_EXPECT(group_name_flag.FilterFlag(selector_args));
  auto instance_name_opt =
      CF_EXPECT(instance_name_flag.FilterFlag(selector_args));

  NameFlagsParam name_flags_param{.group_name = group_name_opt,
                                  .instance_names = instance_name_opt};
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

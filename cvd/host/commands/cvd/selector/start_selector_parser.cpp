/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/selector/selector_cmdline_parser.h"

#include <android-base/strings.h>

#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/selector/selector_option_parser_utils.h"

namespace cuttlefish {
namespace selector {

Result<SelectorFlagsParser> SelectorFlagsParser::ConductSelectFlagsParser(
    const std::vector<std::string>& args) {
  SelectorFlagsParser parser(args);
  CF_EXPECT(parser.ParseOptions(), "selector option flag parsing failed.");
  return {std::move(parser)};
}

SelectorFlagsParser::SelectorFlagsParser(const std::vector<std::string>& args)
    : args_(args) {}

std::optional<std::vector<std::string>> SelectorFlagsParser::Names() const {
  return names_;
}

std::optional<std::string> SelectorFlagsParser::GroupName() const {
  return group_name_;
}

std::optional<std::vector<std::string>> SelectorFlagsParser::PerInstanceNames()
    const {
  return instance_names_;
}

Result<SelectorFlagsParser::ParsedNameFlags> SelectorFlagsParser::HandleNames(
    const std::optional<std::string>& names) const {
  CF_EXPECT(names && !names.value().empty());

  auto name_list = CF_EXPECT(SeparateButWithNoEmptyToken(names.value(), ","));

  // see if they are all device_name
  if (IsValidDeviceName(*name_list.cbegin())) {
    auto device_names_pair = CF_EXPECT(HandleDeviceNames(names));
    return {ParsedNameFlags{
        .names = std::nullopt,
        .group_name = std::move(device_names_pair.group_name),
        .instance_names = std::move(device_names_pair.instance_names)}};
  }

  for (const auto& name : name_list) {
    if (!IsValidDeviceName(name)) {
      return {ParsedNameFlags{
          .names = std::nullopt,
          .group_name = std::nullopt,
          .instance_names = std::move(CF_EXPECT(HandleInstanceNames(names)))}};
    }
  }

  return {ParsedNameFlags{.names = std::move(name_list),
                          .group_name = std::nullopt,
                          .instance_names = std::nullopt}};
}

Result<std::vector<std::string>> SelectorFlagsParser::HandleInstanceNames(
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

Result<std::string> SelectorFlagsParser::HandleGroupName(
    const std::optional<std::string>& group_name) const {
  CF_EXPECT(group_name && !group_name.value().empty());
  CF_EXPECT(IsValidGroupName(group_name.value()));
  return {group_name.value()};
}

Result<SelectorFlagsParser::DeviceNamesPair>
SelectorFlagsParser::HandleDeviceNames(
    const std::optional<std::string>& device_names) const {
  CF_EXPECT(device_names && !device_names.value().empty());

  auto device_name_list =
      CF_EXPECT(SeparateButWithNoEmptyToken(device_names.value(), ","));
  std::unordered_set<std::string> group_names;
  std::vector<std::string> instance_names;
  for (const auto& device_name : device_name_list) {
    CF_EXPECT(IsValidDeviceName(device_name));
    auto [group, instance] = CF_EXPECT(SplitDeviceName(device_name));
    CF_EXPECT(IsValidGroupName(group) && IsValidInstanceName(instance));
    group_names.insert(group);
    instance_names.emplace_back(instance);
  }
  CF_EXPECT(group_names.size() <= 1, "Group names in --device_name options"
                                         << " must be same across devices.");
  const auto group_name = *(group_names.cbegin());
  std::optional<std::string> joined_instance_names =
      android::base::Join(instance_names, ",");
  return {DeviceNamesPair{.group_name = group_name,
                          .instance_names = std::move(CF_EXPECT(
                              HandleInstanceNames(joined_instance_names)))}};
}

Result<SelectorFlagsParser::ParsedNameFlags>
SelectorFlagsParser::HandleNameOpts(const NameFlagsParam& name_flags) const {
  const std::optional<std::string>& names = name_flags.names;
  const std::optional<std::string>& device_names = name_flags.device_names;
  const std::optional<std::string>& group_name = name_flags.group_name;
  const std::optional<std::string>& instance_names = name_flags.instance_names;

  CF_EXPECT(VerifyNameOptions(
      VerifyNameOptionsParam{.name = names,
                             .device_name = device_names,
                             .group_name = group_name,
                             .per_instance_name = instance_names}));

  if (device_names) {
    auto device_names_pair = CF_EXPECT(HandleDeviceNames(device_names));
    return {ParsedNameFlags{
        .names = std::nullopt,
        .group_name = std::move(device_names_pair.group_name),
        .instance_names = std::move(device_names_pair.instance_names)}};
  }

  if (names) {
    auto parsed_name_flags = CF_EXPECT(HandleNames(names));
    return {parsed_name_flags};
  }

  std::optional<std::string> group_name_output;
  std::optional<std::vector<std::string>> instance_names_output;
  if (group_name) {
    group_name_output = CF_EXPECT(HandleGroupName(group_name));
  }

  if (instance_names) {
    instance_names_output =
        std::move(CF_EXPECT(HandleInstanceNames(instance_names)));
  }
  return {ParsedNameFlags{.group_name = std::move(group_name_output),
                          .instance_names = std::move(instance_names_output),
                          .names = std::nullopt}};
}

Result<void> SelectorFlagsParser::ParseOptions() {
  // Handling name-related options
  std::optional<std::string> names;
  std::optional<std::string> device_name;
  std::optional<std::string> group_name;
  std::optional<std::string> instance_name;

  std::unordered_map<std::string, std::optional<std::string>> key_optional_map =
      {
          {kNameOpt, std::optional<std::string>{}},
          {kDeviceNameOpt, std::optional<std::string>{}},
          {kGroupNameOpt, std::optional<std::string>{}},
          {kInstanceNameOpt, std::optional<std::string>{}},
      };

  for (auto& [flag_name, value] : key_optional_map) {
    // value is set to std::nullopt if parsing failed or no flag_name flag is
    // given.
    CF_EXPECT(FilterSelectorFlag(args_, flag_name, value));
  }

  NameFlagsParam name_flags_param{
      .names = key_optional_map[kNameOpt],
      .device_names = key_optional_map[kDeviceNameOpt],
      .group_name = key_optional_map[kGroupNameOpt],
      .instance_names = key_optional_map[kInstanceNameOpt]};
  auto parsed_name_flags = CF_EXPECT(HandleNameOpts(name_flags_param));
  group_name_ = parsed_name_flags.group_name;
  names_ = parsed_name_flags.names;
  instance_names_ = parsed_name_flags.instance_names;

  if (args_.empty()) {
    return {};
  }
  substring_queries_ = CF_EXPECT(FindSubstringsToMatch());
  return {};
}

/*
 * The remaining arguments must be like:
 *  substr0 substr1,substr2,subtr3 ...
 */
Result<std::unordered_set<std::string>>
SelectorFlagsParser::FindSubstringsToMatch() {
  std::unordered_set<std::string> substring_queries;
  const auto args_size = args_.size();
  for (int i = 0; i < args_size; i++) {
    /*
     * Logically, the order does not matter. The reason why we start from
     * behind is that pop_back() of a vector is much cheaper than pop_front()
     */
    const auto& substring = args_.back();
    auto tokens = android::base::Split(substring, ",");
    for (const auto& t : tokens) {
      CF_EXPECT(!t.empty(),
                "Empty keyword for substring search is not allowed.");
      substring_queries_.insert(t);
    }
    args_.pop_back();
  }
  return {substring_queries};
}

bool SelectorFlagsParser::IsValidName(const std::string& name) const {
  return IsValidGroupName(name) || IsValidInstanceName(name) ||
         IsValidDeviceName(name);
}

}  // namespace selector
}  // namespace cuttlefish

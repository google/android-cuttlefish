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

#include <sstream>
#include <string_view>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/selector/selector_option_parser_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace selector {

static Result<unsigned> ParseNaturalNumber(const std::string& token) {
  std::int32_t value;
  CF_EXPECT(android::base::ParseInt(token, &value));
  CF_EXPECT(value > 0);
  return static_cast<unsigned>(value);
}

Result<SelectorFlagsParser> SelectorFlagsParser::ConductSelectFlagsParser(
    const std::vector<std::string>& selector_args,
    const std::vector<std::string>& cmd_args,
    const std::unordered_map<std::string, std::string>& envs) {
  SelectorFlagsParser parser(selector_args, cmd_args, envs);
  CF_EXPECT(parser.ParseOptions(), "selector option flag parsing failed.");
  return {std::move(parser)};
}

SelectorFlagsParser::SelectorFlagsParser(
    const std::vector<std::string>& selector_args,
    const std::vector<std::string>& cmd_args,
    const std::unordered_map<std::string, std::string>& envs)
    : selector_args_(selector_args), cmd_args_(cmd_args), envs_(envs) {}

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
        .group_name = std::move(device_names_pair.group_name),
        .instance_names = std::move(device_names_pair.instance_names)}};
  }

  // they must be either group names or per-instance names
  for (const auto& name : name_list) {
    if (!IsValidGroupName(name)) {
      return {ParsedNameFlags{
          .group_name = std::nullopt,
          .instance_names = std::move(CF_EXPECT(HandleInstanceNames(names)))}};
    }
  }

  // all of the names are either group name or instance name
  // However, the number of group names must be 1.
  if (name_list.size() > 1) {
    return {ParsedNameFlags{
        .group_name = std::nullopt,
        .instance_names = std::move(CF_EXPECT(HandleInstanceNames(names)))}};
  }

  // Now, we have one token that we don't know if this is name or group
  // However, historically, this should be meant to a group name
  auto sole_element = *(name_list.cbegin());
  return {
      ParsedNameFlags{.group_name = CF_EXPECT(HandleGroupName(sole_element)),
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
                          .instance_names = std::move(instance_names_output)}};
}

namespace {

using Envs = std::unordered_map<std::string, std::string>;

std::optional<unsigned> TryFromCuttlefishInstance(const Envs& envs) {
  if (!Contains(envs, kCuttlefishInstanceEnvVarName)) {
    return std::nullopt;
  }
  const auto cuttlefish_instance = envs.at(kCuttlefishInstanceEnvVarName);
  if (cuttlefish_instance.empty()) {
    return std::nullopt;
  }
  auto parsed = ParseNaturalNumber(cuttlefish_instance);
  return parsed.ok() ? std::optional(*parsed) : std::nullopt;
}

std::optional<unsigned> TryFromUser(const Envs& envs) {
  if (!Contains(envs, "USER")) {
    return std::nullopt;
  }
  std::string_view user{envs.at("USER")};
  if (user.empty() || !android::base::ConsumePrefix(&user, kVsocUserPrefix)) {
    return std::nullopt;
  }
  const auto& vsoc_num = user;
  auto vsoc_id = ParseNaturalNumber(vsoc_num.data());
  return vsoc_id.ok() ? std::optional(*vsoc_id) : std::nullopt;
}

}  // namespace

std::optional<std::unordered_set<unsigned>>
SelectorFlagsParser::InstanceFromEnvironment(
    const InstanceFromEnvParam& params) {
  const auto& cuttlefish_instance_env = params.cuttlefish_instance_env;
  const auto& vsoc_suffix = params.vsoc_suffix;
  const auto& num_instances = params.num_instances;

  // see the logic in cuttlefish::InstanceFromEnvironment()
  // defined in host/libs/config/cuttlefish_config.cpp
  std::unordered_set<unsigned> nums;
  std::optional<unsigned> base;
  if (cuttlefish_instance_env) {
    base = *cuttlefish_instance_env;
  }
  if (!base && vsoc_suffix) {
    base = *vsoc_suffix;
  }
  if (!base) {
    return std::nullopt;
  }
  // this is guaranteed by the caller
  // assert(num_instances != std::nullopt);
  for (unsigned i = 0; i != *num_instances; i++) {
    nums.insert(base.value() + i);
  }
  return nums;
}

Result<unsigned> SelectorFlagsParser::VerifyNumOfInstances(
    const VerifyNumOfInstancesParam& params,
    const unsigned default_n_instances) const {
  const auto& num_instances_flag = params.num_instances_flag;
  const auto& instance_names = params.instance_names;
  const auto& instance_nums_flag = params.instance_nums_flag;

  std::optional<unsigned> num_instances;
  if (num_instances_flag) {
    num_instances = CF_EXPECT(ParseNaturalNumber(*num_instances_flag));
  }
  if (instance_names && !instance_names->empty()) {
    auto implied_n_instances = instance_names->size();
    if (num_instances) {
      CF_EXPECT_EQ(*num_instances, static_cast<unsigned>(implied_n_instances),
                   "The number of instances requested by --num_instances "
                       << " are not the same as what is implied by "
                       << " --name/device_name/instance_name.");
    }
    num_instances = implied_n_instances;
  }
  if (instance_nums_flag) {
    std::vector<std::string> tokens =
        android::base::Split(*instance_nums_flag, ",");
    for (const auto& t : tokens) {
      CF_EXPECT(ParseNaturalNumber(t), t << " must be a natural number");
    }
    if (!num_instances) {
      num_instances = tokens.size();
    }
    CF_EXPECT_EQ(*num_instances, tokens.size(),
                 "All information for the number of instances must match.");
  }
  return num_instances.value_or(default_n_instances);
}

Result<SelectorFlagsParser::ParsedInstanceIdsOpt>
SelectorFlagsParser::HandleInstanceIds(
    const InstanceIdsParams& instance_id_params) {
  const auto& instance_nums = instance_id_params.instance_nums;
  const auto& base_instance_num = instance_id_params.base_instance_num;
  const auto& cuttlefish_instance_env =
      instance_id_params.cuttlefish_instance_env;
  const auto& vsoc_suffix = instance_id_params.vsoc_suffix;

  // calculate and/or verify the number of instances
  unsigned num_instances =
      CF_EXPECT(VerifyNumOfInstances(VerifyNumOfInstancesParam{
          .num_instances_flag = instance_id_params.num_instances,
          .instance_names = instance_names_,
          .instance_nums_flag = instance_nums}));

  if (!instance_nums && !base_instance_num) {
    // num_instances is given. if non-std::nullopt is returned,
    // the base is also figured out. If base can't be figured out,
    // std::nullopt is returned.
    auto instance_ids = InstanceFromEnvironment(
        {.cuttlefish_instance_env = cuttlefish_instance_env,
         .vsoc_suffix = vsoc_suffix,
         .num_instances = num_instances});
    if (instance_ids) {
      return ParsedInstanceIdsOpt(*instance_ids);
    }
    // the return value, n_instances is the "desired/requested" instances
    // When instance_ids set isn't figured out, n_instances is not meant to
    // be always zero; it could be any natural number.
    return ParsedInstanceIdsOpt(num_instances);
  }

  InstanceNumsCalculator calculator;
  calculator.NumInstances(static_cast<std::int32_t>(num_instances));
  if (instance_nums) {
    calculator.InstanceNums(*instance_nums);
  }
  if (base_instance_num) {
    unsigned base = CF_EXPECT(ParseNaturalNumber(*base_instance_num));
    calculator.BaseInstanceNum(static_cast<std::int32_t>(base));
  }
  auto instance_ids = std::move(CF_EXPECT(calculator.CalculateFromFlags()));
  CF_EXPECT(!instance_ids.empty(),
            "CalculateFromFlags() must be called when --num_instances or "
                << "--base_instance_num is given, and must not return an "
                << "empty set");
  auto instance_ids_hash_set =
      std::unordered_set<unsigned>{instance_ids.begin(), instance_ids.end()};
  return ParsedInstanceIdsOpt{instance_ids_hash_set};
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
    CF_EXPECT(FilterSelectorFlag(selector_args_, flag_name, value));
  }

  NameFlagsParam name_flags_param{
      .names = key_optional_map[kNameOpt],
      .device_names = key_optional_map[kDeviceNameOpt],
      .group_name = key_optional_map[kGroupNameOpt],
      .instance_names = key_optional_map[kInstanceNameOpt]};
  auto parsed_name_flags = CF_EXPECT(HandleNameOpts(name_flags_param));
  group_name_ = parsed_name_flags.group_name;
  instance_names_ = parsed_name_flags.instance_names;

  std::optional<std::string> num_instances;
  std::optional<std::string> instance_nums;
  std::optional<std::string> base_instance_num;
  // set num_instances as std::nullptr or the value of --num_instances
  FilterSelectorFlag(cmd_args_, "num_instances", num_instances);
  FilterSelectorFlag(cmd_args_, "instance_nums", instance_nums);
  FilterSelectorFlag(cmd_args_, "base_instance_num", base_instance_num);

  InstanceIdsParams instance_nums_param{
      .num_instances = std::move(num_instances),
      .instance_nums = std::move(instance_nums),
      .base_instance_num = std::move(base_instance_num),
      .cuttlefish_instance_env = TryFromCuttlefishInstance(envs_),
      .vsoc_suffix = TryFromUser(envs_)};
  auto parsed_ids = CF_EXPECT(HandleInstanceIds(instance_nums_param));
  requested_num_instances_ = parsed_ids.GetNumOfInstances();
  instance_ids_ = std::move(parsed_ids.GetInstanceIds());

  if (selector_args_.empty()) {
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
  const auto selector_args_size = selector_args_.size();
  for (int i = 0; i < selector_args_size; i++) {
    /*
     * Logically, the order does not matter. The reason why we start from
     * behind is that pop_back() of a vector is much cheaper than pop_front()
     */
    const auto& substring = selector_args_.back();
    auto tokens = android::base::Split(substring, ",");
    for (const auto& t : tokens) {
      CF_EXPECT(!t.empty(),
                "Empty keyword for substring search is not allowed.");
      substring_queries_.insert(t);
    }
    selector_args_.pop_back();
  }
  return {substring_queries};
}

bool SelectorFlagsParser::IsValidName(const std::string& name) const {
  return IsValidGroupName(name) || IsValidInstanceName(name) ||
         IsValidDeviceName(name);
}

}  // namespace selector
}  // namespace cuttlefish

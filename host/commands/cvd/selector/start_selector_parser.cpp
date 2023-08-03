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

#include "host/commands/cvd/selector/start_selector_parser.h"

#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string_view>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/selector/selector_option_parser_utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace std {

/* For a needed CF_EXPECT_EQ(vector, vector, msg) below
 *
 * the result.h included above requires this operator. The declaration must come
 * before the header file if the operator<< is in cuttlefish namespace.
 * Otherwise, operator<< should be in std.
 *
 * The namespace resolution rule is to search cuttlefish, and then std if
 * failed.
 */
static inline std::ostream& operator<<(std::ostream& out,
                                       const std::vector<std::string>& v) {
  if (v.empty()) {
    out << "{}";
    return out;
  }
  out << "{";
  if (v.size() > 1) {
    for (auto itr = v.cbegin(); itr != v.cend() - 1; itr++) {
      out << *itr << ", ";
    }
  }
  out << v.back() << "}";
  return out;
}

}  // namespace std

namespace cuttlefish {
namespace selector {

static bool Unique(const std::vector<unsigned>& v) {
  std::unordered_set<unsigned> hash_set(v.begin(), v.end());
  return v.size() == hash_set.size();
}

static Result<unsigned> ParseNaturalNumber(const std::string& token) {
  std::int32_t value;
  CF_EXPECT(android::base::ParseInt(token, &value));
  CF_EXPECT(value > 0);
  return static_cast<unsigned>(value);
}

Result<StartSelectorParser> StartSelectorParser::ConductSelectFlagsParser(
    const uid_t uid, const cvd_common::Args& selector_args,
    const cvd_common::Args& cmd_args, const cvd_common::Envs& envs) {
  const std::string system_wide_home = CF_EXPECT(SystemWideUserHome(uid));
  cvd_common::Args selector_args_copied{selector_args};
  StartSelectorParser parser(
      system_wide_home, selector_args_copied, cmd_args, envs,
      CF_EXPECT(SelectorCommonParser::Parse(uid, selector_args_copied, envs)));
  CF_EXPECT(parser.ParseOptions(), "selector option flag parsing failed.");
  return {std::move(parser)};
}

StartSelectorParser::StartSelectorParser(
    const std::string& system_wide_user_home,
    const cvd_common::Args& selector_args, const cvd_common::Args& cmd_args,
    const cvd_common::Envs& envs, SelectorCommonParser&& common_parser)
    : client_user_home_{system_wide_user_home},
      selector_args_(selector_args),
      cmd_args_(cmd_args),
      envs_(envs),
      common_parser_(std::move(common_parser)) {}

std::optional<std::string> StartSelectorParser::GroupName() const {
  return group_name_;
}

std::optional<std::vector<std::string>> StartSelectorParser::PerInstanceNames()
    const {
  return per_instance_names_;
}

namespace {

std::optional<unsigned> TryFromCuttlefishInstance(
    const cvd_common::Envs& envs) {
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

}  // namespace

std::optional<std::vector<unsigned>>
StartSelectorParser::InstanceFromEnvironment(
    const InstanceFromEnvParam& params) {
  const auto& cuttlefish_instance_env = params.cuttlefish_instance_env;
  const auto& vsoc_suffix = params.vsoc_suffix;
  const auto& num_instances = params.num_instances;

  // see the logic in cuttlefish::InstanceFromEnvironment()
  // defined in host/libs/config/cuttlefish_config.cpp
  std::vector<unsigned> nums;
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
    nums.emplace_back(base.value() + i);
  }
  return nums;
}

Result<unsigned> StartSelectorParser::VerifyNumOfInstances(
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
                       << " --instance_name.");
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

static Result<std::vector<unsigned>> ParseInstanceNums(
    const std::string& instance_nums_flag) {
  std::vector<unsigned> nums;
  std::vector<std::string> tokens =
      android::base::Split(instance_nums_flag, ",");
  for (const auto& t : tokens) {
    unsigned num =
        CF_EXPECT(ParseNaturalNumber(t), t << " must be a natural number");
    nums.emplace_back(num);
  }
  CF_EXPECT(Unique(nums), "--instance_nums include duplicated numbers");
  return nums;
}

Result<StartSelectorParser::ParsedInstanceIdsOpt>
StartSelectorParser::HandleInstanceIds(
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
          .instance_names = PerInstanceNames(),
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
    CF_EXPECT(base_instance_num == std::nullopt,
              "-base_instance_num and -instance_nums are mutually exclusive.");
    std::vector<unsigned> parsed_nums =
        CF_EXPECT(ParseInstanceNums(*instance_nums));
    return ParsedInstanceIdsOpt(parsed_nums);
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
  auto instance_ids_vector =
      std::vector<unsigned>{instance_ids.begin(), instance_ids.end()};
  return ParsedInstanceIdsOpt{instance_ids_vector};
}

Result<bool> StartSelectorParser::CalcMayBeDefaultGroup() {
  auto disable_default_group_flag = CF_EXPECT(
      SelectorFlags::Get().GetFlag(SelectorFlags::kDisableDefaultGroup));
  if (CF_EXPECT(
          disable_default_group_flag.CalculateFlag<bool>(selector_args_))) {
    return false;
  }
  /*
   * --disable_default_group instructs that the default group
   * should be disabled anyway. If not given, the logic to determine
   * whether this group is the default one or not is:
   *  If HOME is not overridden and no selector options, then
   *   the default group
   *  Or, not a default group
   *
   */
  if (CF_EXPECT(common_parser_.HomeOverridden())) {
    return false;
  }
  return !common_parser_.HasDeviceSelectOption();
}

static bool IsTrue(const std::string& value) {
  std::unordered_set<std::string> true_strings = {"y", "yes", "true"};
  std::string value_in_lower_case = value;
  /*
   * https://en.cppreference.com/w/cpp/string/byte/tolower
   *
   * char should be converted to unsigned char first.
   */
  std::transform(value_in_lower_case.begin(), value_in_lower_case.end(),
                 value_in_lower_case.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return Contains(true_strings, value_in_lower_case);
}

static bool IsFalse(const std::string& value) {
  std::unordered_set<std::string> false_strings = {"n", "no", "false"};
  std::string value_in_lower_case = value;
  /*
   * https://en.cppreference.com/w/cpp/string/byte/tolower
   *
   * char should be converted to unsigned char first.
   */
  std::transform(value_in_lower_case.begin(), value_in_lower_case.end(),
                 value_in_lower_case.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return Contains(false_strings, value_in_lower_case);
}

static std::optional<std::string> GetAcquireFileLockEnvValue(
    const cvd_common::Envs& envs) {
  if (!Contains(envs, SelectorFlags::kAcquireFileLockEnv)) {
    return std::nullopt;
  }
  auto env_value = envs.at(SelectorFlags::kAcquireFileLockEnv);
  if (env_value.empty()) {
    return std::nullopt;
  }
  return env_value;
}

Result<bool> StartSelectorParser::CalcAcquireFileLock() {
  // if the flag is set, flag has the highest priority
  auto must_acquire_file_lock_flag =
      CF_EXPECT(SelectorFlags::Get().GetFlag(SelectorFlags::kAcquireFileLock));
  std::optional<bool> value_opt =
      CF_EXPECT(must_acquire_file_lock_flag.FilterFlag<bool>(selector_args_));
  if (value_opt) {
    return *value_opt;
  }
  // flag is not set. see if there is the environment variable set
  auto env_value_opt = GetAcquireFileLockEnvValue(envs_);
  if (env_value_opt) {
    auto value_string = *env_value_opt;
    if (IsTrue(value_string)) {
      return true;
    }
    if (IsFalse(value_string)) {
      return false;
    }
    return CF_ERR("In \"" << SelectorFlags::kAcquireFileLockEnv << "="
                          << value_string << ",\" \"" << value_string
                          << "\" is an invalid value. Try true or false.");
  }
  // nothing set, falls back to the default value of the flag
  auto default_value =
      CF_EXPECT(must_acquire_file_lock_flag.DefaultValue<bool>());
  return default_value;
}

Result<StartSelectorParser::WebrtcCalculatedNames>
StartSelectorParser::CalcNamesUsingWebrtcDeviceId() {
  std::optional<std::string> webrtc_device_ids_opt;
  FilterSelectorFlag(cmd_args_, "webrtc_device_id", webrtc_device_ids_opt);
  if (!webrtc_device_ids_opt) {
    return WebrtcCalculatedNames{
        .group_name = common_parser_.GroupName(),
        .per_instance_names = common_parser_.PerInstanceNames()};
  }
  const std::string webrtc_device_ids =
      std::move(webrtc_device_ids_opt.value());
  std::vector<std::string> webrtc_device_names =
      android::base::Tokenize(webrtc_device_ids, ",");

  std::unordered_set<std::string> group_names;
  std::vector<std::string> instance_names;
  instance_names.reserve(webrtc_device_names.size());

  // check if the supposedly group names exist and common across each
  // webrtc_device_id
  for (const auto& webrtc_device_name : webrtc_device_names) {
    std::vector<std::string> tokens =
        android::base::Tokenize(webrtc_device_name, "-");
    CF_EXPECT_GE(tokens.size(), 2,
                 webrtc_device_name
                     << " cannot be split into group name and instance name");
    group_names.insert(tokens.front());
    CF_EXPECT_EQ(group_names.size(), 1,
                 "group names in --webrtc_device_id must be the same but are "
                 "different.");
    tokens.erase(tokens.begin());
    instance_names.push_back(android::base::Join(tokens, "-"));
  }

  std::string group_name = *(group_names.begin());
  CF_EXPECT(IsValidGroupName(group_name),
            group_name << " is not a valid group name");

  for (const auto& instance_name : instance_names) {
    CF_EXPECT(IsValidInstanceName(instance_name),
              instance_name << " is not a valid instance name.");
  }

  if (auto flag_group_name_opt = common_parser_.GroupName()) {
    CF_EXPECT_EQ(flag_group_name_opt.value(), group_name);
  }
  if (auto flag_per_instance_names_opt = common_parser_.PerInstanceNames()) {
    CF_EXPECT_EQ(flag_per_instance_names_opt.value(), instance_names);
  }
  return WebrtcCalculatedNames{.group_name = group_name,
                               .per_instance_names = instance_names};
}

Result<void> StartSelectorParser::ParseOptions() {
  may_be_default_group_ = CF_EXPECT(CalcMayBeDefaultGroup());
  must_acquire_file_lock_ = CF_EXPECT(CalcAcquireFileLock());

  // compare webrtc_device_id against instance names
  auto verified_names =
      CF_EXPECT(CalcNamesUsingWebrtcDeviceId(),
                "--webrtc_device_id must match the list of device names");
  group_name_ = verified_names.group_name;
  per_instance_names_ = verified_names.per_instance_names;

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
      .cuttlefish_instance_env = TryFromCuttlefishInstance(envs_)};
  auto parsed_ids = CF_EXPECT(HandleInstanceIds(instance_nums_param));
  requested_num_instances_ = parsed_ids.GetNumOfInstances();
  instance_ids_ = std::move(parsed_ids.GetInstanceIds());

  return {};
}

}  // namespace selector
}  // namespace cuttlefish

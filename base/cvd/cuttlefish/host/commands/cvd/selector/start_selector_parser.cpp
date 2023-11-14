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
  /*
   * the logic to determine whether this group is the default one or not:
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

Result<void> StartSelectorParser::ParseOptions() {
  may_be_default_group_ = CF_EXPECT(CalcMayBeDefaultGroup());
  must_acquire_file_lock_ = CF_EXPECT(CalcAcquireFileLock());

  group_name_ = common_parser_.GroupName();
  per_instance_names_ = common_parser_.PerInstanceNames();

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

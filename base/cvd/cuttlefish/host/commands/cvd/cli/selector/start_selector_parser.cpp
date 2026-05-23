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

#include "cuttlefish/host/commands/cvd/cli/selector/start_selector_parser.h"

#include <stdint.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_option_parser_utils.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace selector {

static bool Unique(const std::vector<unsigned>& v) {
  std::unordered_set<unsigned> hash_set(v.begin(), v.end());
  return v.size() == hash_set.size();
}

static Result<unsigned> ParsePositiveNumber(std::string_view token) {
  unsigned value;
  CF_EXPECT(absl::SimpleAtoi(token, &value));
  CF_EXPECT(value > 0);
  return value;
}

Result<StartSelectorParser> StartSelectorParser::ConductSelectFlagsParser(
    const std::optional<std::vector<std::string>>& instance_names_opt,
    const cvd_common::Args& cmd_args, const cvd_common::Envs& envs) {
  StartSelectorParser parser(instance_names_opt, cmd_args, envs);
  CF_EXPECT(parser.ParseOptions(), "selector option flag parsing failed.");
  return {std::move(parser)};
}

StartSelectorParser::StartSelectorParser(
    const std::optional<std::vector<std::string>>& instance_names_opt,
    const cvd_common::Args& cmd_args, const cvd_common::Envs& envs)
    : cmd_args_(cmd_args), envs_(envs) {
  if (instance_names_opt) {
    num_instance_names_opt_ = instance_names_opt->size();
  }
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
  auto parsed = ParsePositiveNumber(cuttlefish_instance);
  return parsed.ok() ? std::optional(*parsed) : std::nullopt;
}

}  // namespace

std::optional<std::vector<unsigned>>
StartSelectorParser::InstanceFromEnvironment(
    const InstanceFromEnvParam& params) {
  const auto& cuttlefish_instance_env = params.cuttlefish_instance_env;
  const auto& num_instances = params.num_instances;

  // see the logic in cuttlefish::InstanceFromEnvironment()
  // defined in host/libs/config/cuttlefish_config.cpp
  std::vector<unsigned> nums;
  std::optional<unsigned> base;
  if (cuttlefish_instance_env) {
    base = *cuttlefish_instance_env;
  }
  if (!base) {
    return {};
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
  const auto& num_instance_names = params.num_instance_names;
  const auto& instance_nums_flag = params.instance_nums_flag;

  std::optional<unsigned> num_instances;
  if (num_instances_flag) {
    num_instances = CF_EXPECT(ParsePositiveNumber(*num_instances_flag));
  }
  if (num_instance_names.value_or(0) > 0) {
    auto implied_n_instances = *num_instance_names;
    if (num_instances) {
      CF_EXPECT_EQ(*num_instances, static_cast<unsigned>(implied_n_instances),
                   "The number of instances requested by --num_instances "
                       << " are not the same as what is implied by "
                       << " --instance_name.");
    }
    num_instances = implied_n_instances;
  }
  if (instance_nums_flag) {
    std::vector<std::string_view> tokens =
        absl::StrSplit(*instance_nums_flag, ',');
    for (const auto& t : tokens) {
      CF_EXPECT(ParsePositiveNumber(t), t << " must be a natural number");
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
  std::vector<std::string_view> tokens =
      absl::StrSplit(instance_nums_flag, ',');
  for (const auto& t : tokens) {
    unsigned num =
        CF_EXPECT(ParsePositiveNumber(t), t << " must be a natural number");
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

  // calculate and/or verify the number of instances
  unsigned num_instances =
      CF_EXPECT(VerifyNumOfInstances(VerifyNumOfInstancesParam{
          .num_instances_flag = instance_id_params.num_instances,
          .num_instance_names = num_instance_names_opt_,
          .instance_nums_flag = instance_nums}));

  if (!instance_nums && !base_instance_num) {
    // num_instances is given. if non-std::nullopt is returned,
    // the base is also figured out. If base can't be figured out,
    // std::nullopt is returned.
    auto instance_ids = InstanceFromEnvironment(
        {.cuttlefish_instance_env = cuttlefish_instance_env,
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
  calculator.NumInstances(static_cast<int32_t>(num_instances));
  if (instance_nums) {
    CF_EXPECT(base_instance_num == std::nullopt,
              "-base_instance_num and -instance_nums are mutually exclusive.");
    std::vector<unsigned> parsed_nums =
        CF_EXPECT(ParseInstanceNums(*instance_nums));
    return ParsedInstanceIdsOpt(parsed_nums);
  }
  if (base_instance_num) {
    unsigned base = CF_EXPECT(ParsePositiveNumber(*base_instance_num));
    calculator.BaseInstanceNum(static_cast<int32_t>(base));
  }
  auto instance_ids = CF_EXPECT(calculator.CalculateFromFlags());
  CF_EXPECT(!instance_ids.empty(),
            "CalculateFromFlags() must be called when --num_instances or "
                << "--base_instance_num is given, and must not return an "
                << "empty set");
  auto instance_ids_vector =
      std::vector<unsigned>{instance_ids.begin(), instance_ids.end()};
  return ParsedInstanceIdsOpt{instance_ids_vector};
}

Result<void> StartSelectorParser::ParseOptions() {
  // set num_instances as std::nullopt or the value of --num_instances
  std::optional<std::string> num_instances =
      CF_EXPECT(FilterSelectorFlag<std::string>(cmd_args_, "num_instances"));
  std::optional<std::string> instance_nums =
      CF_EXPECT(FilterSelectorFlag<std::string>(cmd_args_, "instance_nums"));
  std::optional<std::string> base_instance_num = CF_EXPECT(
      FilterSelectorFlag<std::string>(cmd_args_, "base_instance_num"));

  InstanceIdsParams instance_nums_param{
      .num_instances = std::move(num_instances),
      .instance_nums = std::move(instance_nums),
      .base_instance_num = std::move(base_instance_num),
      .cuttlefish_instance_env = TryFromCuttlefishInstance(envs_)};
  auto parsed_ids = CF_EXPECT(HandleInstanceIds(instance_nums_param));
  requested_num_instances_ = parsed_ids.GetNumOfInstances();
  instance_ids_ = parsed_ids.GetInstanceIds();

  return {};
}

}  // namespace selector
}  // namespace cuttlefish

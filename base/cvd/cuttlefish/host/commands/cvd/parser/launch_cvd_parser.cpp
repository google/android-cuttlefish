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
#include "host/commands/cvd/parser/launch_cvd_parser.h"

#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <json/json.h>

#include "android-base/strings.h"

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_configs_instances.h"
#include "host/commands/cvd/parser/cf_metrics_configs.h"
#include "host/commands/cvd/parser/launch_cvd_templates.h"

namespace cuttlefish {
namespace {

std::optional<std::string> GenerateUndefOkFlag(std::vector<std::string>& flags) {
  // TODO(b/1153527): don't pass undefok, pass only the explicitly specified
  // flags instead
  if (flags.empty()) {
    return {};
  }
  std::vector<std::string> flag_names;
  std::regex dashes_re("^--?");
  std::regex value_re("=.*$");
  for (const auto& flag: flags) {
    auto flag_without_dashes = std::regex_replace(flag, dashes_re, "");
    auto flag_name = std::regex_replace(flag_without_dashes, value_re, "");
    flag_names.emplace_back(std::move(flag_name));
  }
  return "--undefok=" + android::base::Join(flag_names, ',');
}

Result<std::vector<std::string>> GenerateCfFlags(const Json::Value& root) {
  std::vector<std::string> result;
  result.emplace_back(GenerateGflag(
      "num_instances", {std::to_string(root["instances"].size())}));
  result.emplace_back(GenerateGflag(
      "netsim_bt", {CF_EXPECT(GetValue<std::string>(root, {"netsim_bt"}))}));
  result.emplace_back(GenerateGflag(
      "netsim_uwb", {CF_EXPECT(GetValue<std::string>(root, {"netsim_uwb"}))}));
  result = MergeResults(result, CF_EXPECT(GenerateMetricsFlags(root)));
  result = MergeResults(result,
                        CF_EXPECT(GenerateInstancesFlags(root["instances"])));
  auto flag_op = GenerateUndefOkFlag(result);
  if (flag_op.has_value()) {
    result.emplace_back(std::move(*flag_op));
  }
  return result;
}

Result<void> InitCvdConfigs(Json::Value& root) {
  CF_EXPECT(InitConfig(root, CF_DEFAULTS_NETSIM_BT, {"netsim_bt"}));
  CF_EXPECT(InitConfig(root, CF_DEFAULTS_NETSIM_UWB, {"netsim_uwb"}));
  CF_EXPECT(InitMetricsConfigs(root));
  CF_EXPECT(InitInstancesConfigs(root["instances"]));
  return {};
}

}  // namespace

Result<std::vector<std::string>> ParseLaunchCvdConfigs(Json::Value& root) {
  ExtractLaunchTemplates(root["instances"]);
  CF_EXPECT(InitCvdConfigs(root));
  return GenerateCfFlags(root);
}

}  // namespace cuttlefish

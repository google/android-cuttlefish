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

#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_configs_instances.h"
#include "host/commands/cvd/parser/cf_metrics_configs.h"
#include "host/commands/cvd/parser/launch_cvd_templates.h"

namespace cuttlefish {
namespace {

Result<std::vector<std::string>> GenerateCfFlags(const Json::Value& root) {
  std::vector<std::string> result;
  result.emplace_back(GenerateGflag(
      "num_instances", {std::to_string(root["instances"].size())}));
  result.emplace_back(GenerateGflag(
      "netsim_bt", {CF_EXPECT(GetValue<std::string>(root, {"netsim_bt"}))}));
  result = MergeResults(result, GenerateMetricsFlags(root["metrics"]));
  result = MergeResults(result,
                        CF_EXPECT(GenerateInstancesFlags(root["instances"])));
  return result;
}

Result<void> InitCvdConfigs(Json::Value& root) {
  CF_EXPECT(InitConfig(root, CF_DEFAULTS_NETSIM_BT, {"netsim_bt"}));
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

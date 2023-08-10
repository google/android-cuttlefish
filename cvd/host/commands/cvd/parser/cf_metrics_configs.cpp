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
#include "host/commands/cvd/parser/cf_metrics_configs.h"

#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {
namespace {

constexpr bool kEnableMetricsDefault = false;

std::string EnabledToReportAnonUsageStats(const bool enabled) {
  return enabled ? "y" : "n";
}

}  // namespace

Result<void> InitMetricsConfigs(Json::Value& root) {
  CF_EXPECT(InitConfig(root, kEnableMetricsDefault, {"metrics", "enable"}));
  return {};
}

Result<std::vector<std::string>> GenerateMetricsFlags(const Json::Value& root) {
  std::vector<std::string> result;
  auto report_flag_value = EnabledToReportAnonUsageStats(
      CF_EXPECT(GetValue<bool>(root, {"metrics", "enable"})));
  result.emplace_back(
      GenerateGflag("report_anonymous_usage_stats", {report_flag_value}));
  return result;
}

}  // namespace cuttlefish

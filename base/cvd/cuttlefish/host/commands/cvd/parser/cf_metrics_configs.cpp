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

#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/parser/load_config.pb.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {

using cvd::config::Instance;
using cvd::config::Launch;

namespace {

constexpr bool kEnableMetricsDefault = false;

std::string EnabledToReportAnonUsageStats(const bool enabled) {
  return enabled ? "y" : "n";
}

}  // namespace

std::vector<std::string> GenerateMetricsFlags(const Launch& config) {
  bool enable = kEnableMetricsDefault;
  if (config.metrics().has_enable()) {
    enable = config.metrics().enable();
  }
  auto flag_value = EnabledToReportAnonUsageStats(enable);
  return {GenerateFlag("report_anonymous_usage_stats", flag_value)};
}

}  // namespace cuttlefish

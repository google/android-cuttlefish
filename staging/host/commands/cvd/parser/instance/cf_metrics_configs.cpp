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
#include "host/commands/cvd/parser/instance/cf_metrics_configs.h"

#include <android-base/logging.h>

#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/libs/config/cuttlefish_config.h"

// Metrics collection will be disabled by default for canonical configs MVP
#define DEFAULT_ENABLE_REPORTING "n"

namespace cuttlefish {

static std::string GenerateReportFlag() {
  std::stringstream result_flag;
  result_flag << "--report_anonymous_usage_stats=" << DEFAULT_ENABLE_REPORTING;
  return result_flag.str();
}

std::vector<std::string> GenerateMetricsFlags(const Json::Value&) {
  std::vector<std::string> result;
  result.emplace_back(GenerateReportFlag());
  return result;
}

}  // namespace cuttlefish

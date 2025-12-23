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
#include "cuttlefish/host/commands/cvd/cli/parser/launch_cvd_parser.h"

#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "android-base/strings.h"

#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_instances.h"
#include "cuttlefish/host/commands/cvd/cli/parser/cf_metrics_configs.h"
#include "cuttlefish/host/commands/cvd/cli/parser/launch_cvd_templates.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

using cvd::config::EnvironmentSpecification;

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

Result<std::vector<std::string>> GenerateCfFlags(
    const EnvironmentSpecification& launch) {
  std::vector<std::string> flags;
  flags.emplace_back(GenerateFlag("num_instances", launch.instances().size()));

  if (launch.has_netsim_bt()) {
    flags.emplace_back(GenerateFlag("netsim_bt", launch.netsim_bt()));
  }

  if (launch.has_netsim_uwb()) {
    flags.emplace_back(GenerateFlag("netsim_uwb", launch.netsim_uwb()));
  }

  flags = MergeResults(std::move(flags), GenerateMetricsFlags(launch));
  flags = MergeResults(std::move(flags), CF_EXPECT(GenerateInstancesFlags(launch)));
  auto flag_op = GenerateUndefOkFlag(flags);
  if (flag_op.has_value()) {
    flags.emplace_back(std::move(*flag_op));
  }
  return flags;
}

}  // namespace

Result<std::vector<std::string>> ParseLaunchCvdConfigs(
    EnvironmentSpecification launch) {
  launch = CF_EXPECT(ExtractLaunchTemplates(std::move(launch)));

  return GenerateCfFlags(launch);
}

}  // namespace cuttlefish

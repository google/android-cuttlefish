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

#include "host/commands/cvd/selector/creation_analyzer.h"

#include <sys/types.h>

#include <algorithm>
#include <regex>
#include <set>
#include <string>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_cmdline_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace selector {

Result<GroupCreationInfo> CreationAnalyzer::Analyze(
    const CreationAnalyzerParam& param, const std::optional<ucred>& credential,
    InstanceLockFileManager& instance_lock_file_manager) {
  auto selector_options_parser =
      CF_EXPECT(SelectorFlagsParser::ConductSelectFlagsParser(
          param.selector_args, param.cmd_args, param.envs));
  CreationAnalyzer analyzer(param, credential,
                            std::move(selector_options_parser),
                            instance_lock_file_manager);
  auto result = CF_EXPECT(analyzer.Analyze());
  return result;
}

CreationAnalyzer::CreationAnalyzer(
    const CreationAnalyzerParam& param, const std::optional<ucred>& credential,
    SelectorFlagsParser&& selector_options_parser,
    InstanceLockFileManager& instance_file_lock_manager)
    : cmd_args_(param.cmd_args),
      envs_(param.envs),
      selector_args_(param.selector_args),
      credential_(credential),
      selector_options_parser_{std::move(selector_options_parser)},
      instance_file_lock_manager_{instance_file_lock_manager} {}

static void PlaceHolder(InstanceLockFileManager&) {}

Result<std::vector<PerInstanceInfo>>
CreationAnalyzer::AnalyzeInstanceIdsWithLock() {
  // TODO(kwstephenkim): implement AnalyzeInstanceIdsWithLock()
  PlaceHolder(instance_file_lock_manager_);
  return std::vector<PerInstanceInfo>{};
}

Result<GroupCreationInfo> CreationAnalyzer::Analyze() {
  // TODO(kwstephenkim): check if the command is "start"
  auto instance_info = CF_EXPECT(AnalyzeInstanceIdsWithLock());
  group_name_ = AnalyzeGroupName(instance_info);
  home_ = CF_EXPECT(AnalyzeHome());
  // TODO(kwstephenkim): implement host_artifacts_path_
  host_artifacts_path_ = "";

  GroupCreationInfo report = {.home = home_,
                              .host_artifacts_path = host_artifacts_path_,
                              .group_name = group_name_,
                              .instances = std::move(instance_info),
                              .args = cmd_args_,
                              .envs = envs_};
  return report;
}

std::string CreationAnalyzer::AnalyzeGroupName(
    const std::vector<PerInstanceInfo>&) const {
  // TODO(kwstephenkim): implement AnalyzeGroupName()
  return "";
}

Result<std::string> CreationAnalyzer::AnalyzeHome() const {
  // TODO(kwstephenkim): implement AnalyzeHome()
  return "";
}

}  // namespace selector
}  // namespace cuttlefish

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

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct CvdFlags {
  std::vector<std::string> launch_cvd_flags;
  selector::SelectorOptions selector_flags;
  std::vector<std::string> fetch_cvd_flags;
  std::string target_directory;
};

struct LoadFlags {
  std::map<std::string, std::string, std::less<void>> overrides;
  std::string base_dir;
};

std::vector<Flag> BuildCvdLoadFlags(LoadFlags& load_flags);

Result<cvd::config::EnvironmentSpecification> GetEnvironmentSpecification(
    const std::string& config_path,
    const std::map<std::string, std::string, std::less<void>>& overrides);

Result<InstanceManager::GroupDirectories> GetGroupCreationDirectories(
    const std::string& parent_directory,
    const cvd::config::EnvironmentSpecification& env_spec);

Result<CvdFlags> ParseCvdConfigs(
    const cvd::config::EnvironmentSpecification& env_spec,
    const LocalInstanceGroup& group);

};  // namespace cuttlefish

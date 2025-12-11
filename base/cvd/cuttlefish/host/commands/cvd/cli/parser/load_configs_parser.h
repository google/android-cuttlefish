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

#include <ostream>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"

namespace cuttlefish {

struct CvdFlags {
  std::vector<std::string> launch_cvd_flags;
  std::vector<std::string> selector_flags;
  std::vector<std::string> fetch_cvd_flags;
  std::string target_directory;
};

struct Override {
  std::string config_path;
  std::string new_value;
};

std::ostream& operator<<(std::ostream& out, const Override& override);

struct LoadFlags {
  std::vector<Override> overrides;
  std::string config_path;
  std::string credential_source;
  std::string project_id;
  std::string base_dir;
};

Result<LoadFlags> GetFlags(std::vector<std::string>& args,
                           const std::string& working_directory);

Result<cvd::config::EnvironmentSpecification> GetEnvironmentSpecification(
    const LoadFlags& flags);

Result<InstanceManager::GroupDirectories> GetGroupCreationDirectories(
    const std::string& parent_directory,
    const cvd::config::EnvironmentSpecification& env_spec);

Result<CvdFlags> ParseCvdConfigs(
    const cvd::config::EnvironmentSpecification& env_spec,
    const LocalInstanceGroup& group);

};  // namespace cuttlefish

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
#include "cuttlefish/host/commands/cvd/cli/parser/instance/cf_disk_configs.h"

#include <algorithm>
#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/libs/config/data_image_policy.h"

#define DEFAULT_BLANK_DATA_IMAGE_SIZE "unset"

namespace cuttlefish {

using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;

std::vector<std::string> GenerateDiskFlags(
    const EnvironmentSpecification& config) {
  std::vector<std::string> data_image_mbs;
  std::vector<std::string> data_policies;
  if (std::none_of(config.instances().cbegin(), config.instances().cend(),
                   [](const auto& instance) {
                     return instance.disk().has_blank_data_image_mb();
                   })) {
    return {};
  }
  for (const auto& instance : config.instances()) {
    const auto& disk = instance.disk();
    if (disk.has_blank_data_image_mb()) {
      data_image_mbs.emplace_back(std::to_string(disk.blank_data_image_mb()));
      data_policies.emplace_back(
          DataImagePolicyString(DataImagePolicy::AlwaysCreate));
    } else {
      data_image_mbs.emplace_back(DEFAULT_BLANK_DATA_IMAGE_SIZE);
      data_policies.emplace_back(
          DataImagePolicyString(DataImagePolicy::UseExisting));
    }
  }
  return std::vector<std::string>{
      GenerateVecFlag("blank_data_image_mb", data_image_mbs),
      GenerateVecFlag("data_policy", data_policies),
  };
}

}  // namespace cuttlefish

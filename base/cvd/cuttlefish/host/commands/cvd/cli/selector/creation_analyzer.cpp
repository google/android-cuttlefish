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

#include "cuttlefish/host/commands/cvd/cli/selector/creation_analyzer.h"

#include <sys/types.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace selector {
namespace {

Result<std::vector<InstanceParams>> BuildInstanceParams(
    size_t n_instances, std::vector<unsigned> instance_ids,
    const std::optional<std::vector<std::string>>& instance_names_opt) {
  std::vector<InstanceParams> instance_params(n_instances);
  for (size_t i = 0; i < n_instances && i < instance_ids.size(); ++i) {
    instance_params[i].instance_id = instance_ids[i];
  }

  if (instance_names_opt) {
    CF_EXPECT_EQ(instance_names_opt.value().size(), n_instances,
                 "Number of instance names provided doesn't match number of "
                 "requested instances");
    for (size_t i = 0; i < n_instances; ++i) {
      instance_params[i].per_instance_name = instance_names_opt.value()[i];
    }
  }

  return instance_params;
}

// Determines whether the user provided a custom directory in the $HOME variable
// and returns it.
Result<std::optional<std::string>> HomeFromEnvironment(
    const cvd_common::Envs& env) {
  auto home_it = env.find("HOME");
  if (home_it == env.end() ||
      home_it->second == CF_EXPECT(SystemWideUserHome())) {
    return std::nullopt;
  }
  std::string home = home_it->second;
  CF_EXPECT(EnsureDirectoryExists(home),
            "Provided home directory doesn't exist and can't be created");
  return home;
}

}  // namespace

Result<GroupCreationInfo> AnalyzeCreation(const CreationAnalyzerParam& params) {
  InstanceGroupParams group_params;
  group_params.instances =
      CF_EXPECT(BuildInstanceParams(params.num_instances, params.instance_ids,
                                    params.selectors.instance_names));
  group_params.group_name = params.selectors.group_name.value_or("");
  InstanceManager::GroupDirectories group_directories{
      .home = CF_EXPECT(HomeFromEnvironment(params.envs)),
      .host_artifacts_path = CF_EXPECT(AndroidHostPath(params.envs)),
  };
  group_directories.product_out_paths.reserve(params.num_instances);
  auto it = params.envs.find(kAndroidProductOut);
  if (it != params.envs.end()) {
    std::vector<std::string_view> env_product_out =
        absl::StrSplit(it->second, ',');
    if (env_product_out.size() > params.num_instances) {
      LOG(WARNING) << env_product_out.size()
                   << " product paths provided, but only "
                   << params.num_instances << " are going to be created";
      env_product_out.resize(params.num_instances);
    }
    for (auto& env_path : env_product_out) {
      group_directories.product_out_paths.emplace_back(env_path);
    }
  } else {
    group_directories.product_out_paths.emplace_back(
        group_directories.host_artifacts_path);
  }
  while (group_directories.product_out_paths.size() < params.num_instances) {
    // Use the first product path when more instances are required than product
    // paths provided. This supports creating multiple identical instances from
    // a single set of images.
    group_directories.product_out_paths.emplace_back(
        group_directories.product_out_paths[0]);
  }

  return GroupCreationInfo{
      .group_creation_params = group_params,
      .group_directories = group_directories,
  };
}

}  // namespace selector
}  // namespace cuttlefish

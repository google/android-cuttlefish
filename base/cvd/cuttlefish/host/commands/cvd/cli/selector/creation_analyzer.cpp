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
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cli/selector/start_selector_parser.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"

namespace cuttlefish {
namespace selector {
namespace {

class CreationAnalyzer {
 public:
  static Result<CreationAnalyzer> Create(const CreationAnalyzerParam& param);

  Result<GroupCreationInfo> ExtractGroupInfo();

 private:
  CreationAnalyzer(const CreationAnalyzerParam& param,
                   StartSelectorParser&& selector_options_parser);

  Result<std::vector<InstanceParams>> AnalyzeInstances();

  /**
   * Figures out the HOME directory
   *
   * The issue is that many times, HOME is anyway implicitly given. Thus, only
   * if the HOME value is not equal to the HOME directory recognized by the
   * system, it can be safely regarded as overridden by the user.
   *
   * If that is not the case, we use an automatically generated value as HOME.
   */
  Result<std::optional<std::string>> AnalyzeHome() const;

  // inputs
  std::unordered_map<std::string, std::string> envs_;

  // internal, temporary
  StartSelectorParser selector_options_parser_;
};

Result<CreationAnalyzer> CreationAnalyzer::Create(
    const CreationAnalyzerParam& param) {
  auto selector_options_parser =
      CF_EXPECT(StartSelectorParser::ConductSelectFlagsParser(
          param.selectors, param.cmd_args, param.envs));
  return CreationAnalyzer(param, std::move(selector_options_parser));
}

CreationAnalyzer::CreationAnalyzer(
    const CreationAnalyzerParam& param,
    StartSelectorParser&& selector_options_parser)
    : envs_(param.envs),
      selector_options_parser_{std::move(selector_options_parser)} {}

Result<std::vector<InstanceParams>> CreationAnalyzer::AnalyzeInstances() {
  // As this test was done earlier, this line must not fail
  const auto n_instances = selector_options_parser_.RequestedNumInstances();
  std::vector<InstanceParams> instance_params(n_instances);
  std::vector<unsigned> instance_ids = selector_options_parser_.InstanceIds();
  for (size_t i = 0; i < n_instances && i < instance_ids.size(); ++i) {
    instance_params[i].instance_id = instance_ids[i];
  }

  std::optional<std::vector<std::string>> instance_names_opt =
      selector_options_parser_.PerInstanceNames();
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

Result<GroupCreationInfo> CreationAnalyzer::ExtractGroupInfo() {
  InstanceGroupParams group_params;
  group_params.instances = CF_EXPECT(AnalyzeInstances());
  group_params.group_name = selector_options_parser_.GroupName().value_or("");
  InstanceManager::GroupDirectories group_directories{
      .home = CF_EXPECT(AnalyzeHome()),
      .host_artifacts_path = CF_EXPECT(AndroidHostPath(envs_)),
  };
  size_t num_instances = group_params.instances.size();
  group_directories.product_out_paths.reserve(num_instances);
  auto it = envs_.find(kAndroidProductOut);
  if (it != envs_.end()) {
    std::vector<std::string> env_product_out =
        absl::StrSplit(it->second, ',');
    if (env_product_out.size() > num_instances) {
      LOG(WARNING) << env_product_out.size()
                   << " product paths provided, but only " << num_instances
                   << " are going to be created";
      env_product_out.resize(num_instances);
    }
    for (auto& env_path : env_product_out) {
      group_directories.product_out_paths.emplace_back(env_path);
    }
  } else {
    group_directories.product_out_paths.emplace_back(
        group_directories.host_artifacts_path);
  }
  while (group_directories.product_out_paths.size() < num_instances) {
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
}  // namespace

Result<std::optional<std::string>> CreationAnalyzer::AnalyzeHome() const {
  auto home_it = envs_.find("HOME");
  if (home_it == envs_.end() ||
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
  CreationAnalyzer analyzer = CF_EXPECT(CreationAnalyzer::Create(params));
  return CF_EXPECT(analyzer.ExtractGroupInfo());
}

}  // namespace selector
}  // namespace cuttlefish

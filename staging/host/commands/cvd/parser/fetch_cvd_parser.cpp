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

#include "host/commands/cvd/parser/fetch_cvd_parser.h"

#include <algorithm>
#include <string>
#include <vector>

#include <json/json.h>

#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {
namespace {

void InitFetchInstanceConfigs(Json::Value& instances) {
  InitNullGroupConfig(instances, "disk", "default_build");
  InitNullGroupConfig(instances, "disk", "system_build");
  InitNullGroupConfig(instances, "disk", "kernel_build");
}

void InitFetchCvdConfigs(Json::Value& root) {
  InitNullConfig(root, "credential_source");
  InitFetchInstanceConfigs(root["instances"]);
}

bool ShouldFetch(const std::vector<Json::Value>& values) {
  return std::any_of(std::begin(values), std::end(values),
                     [](const Json::Value& value) { return !value.isNull(); });
}

FetchCvdInstanceConfig ParseFetchInstanceConfigs(const Json::Value& instance) {
  Json::Value default_build = instance["disk"]["default_build"];
  Json::Value system_build = instance["disk"]["system_build"];
  Json::Value kernel_build = instance["disk"]["kernel_build"];
  return FetchCvdInstanceConfig{
      .default_build = default_build.asString(),
      .system_build = system_build.asString(),
      .kernel_build = kernel_build.asString(),
      .should_fetch = ShouldFetch({default_build, system_build, kernel_build})};
}

FetchCvdConfig GenerateFetchCvdFlags(const Json::Value& root) {
  FetchCvdConfig result;
  result.credential_source = root["credential_source"].asString();
  int num_instances = root["instances"].size();
  for (unsigned int i = 0; i < num_instances; i++) {
    auto instance_config = ParseFetchInstanceConfigs(root["instances"][i]);
    result.instances.emplace_back(instance_config);
  }

  return result;
}

}  // namespace

FetchCvdConfig ParseFetchCvdConfigs(Json::Value& root) {
  InitFetchCvdConfigs(root);
  return GenerateFetchCvdFlags(root);
}

}  // namespace cuttlefish

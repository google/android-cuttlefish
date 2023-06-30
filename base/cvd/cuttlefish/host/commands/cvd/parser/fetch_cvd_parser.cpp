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
#include <optional>
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
  InitNullConfig(root, "api_key");
  InitNullConfig(root, "credential_source");
  InitNullConfig(root, "wait_retry_period");
  InitNullConfig(root, "external_dns_resolver");
  InitNullConfig(root, "keep_downloaded_archives");
  InitFetchInstanceConfigs(root["instances"]);
}

std::optional<std::string> OptString(const Json::Value& value) {
  if (value.isNull()) {
    return std::nullopt;
  }
  return value.asString();
}

bool ShouldFetch(const std::vector<std::optional<std::string>>& values) {
  return std::any_of(std::begin(values), std::end(values),
                     [](const std::optional<std::string>& value) {
                       return value.has_value();
                     });
}

FetchCvdInstanceConfig ParseFetchInstanceConfigs(const Json::Value& instance) {
  auto result = FetchCvdInstanceConfig{
      .default_build = OptString(instance["disk"]["default_build"]),
      .system_build = OptString(instance["disk"]["system_build"]),
      .kernel_build = OptString(instance["disk"]["kernel_build"])};
  result.should_fetch = ShouldFetch(
      {result.default_build, result.system_build, result.kernel_build});
  return result;
}

FetchCvdConfig GenerateFetchCvdFlags(const Json::Value& root) {
  auto result = FetchCvdConfig{
      .api_key = OptString(root["api_key"]),
      .credential_source = OptString(root["credential_source"]),
      .wait_retry_period = OptString(root["wait_retry_period"]),
      .external_dns_resolver = OptString(root["external_dns_resolver"]),
      .keep_downloaded_archives = OptString(root["keep_downloaded_archives"])};

  const int num_instances = root["instances"].size();
  for (unsigned int i = 0; i < num_instances; i++) {
    result.instances.emplace_back(
        ParseFetchInstanceConfigs(root["instances"][i]));
  }
  return result;
}

}  // namespace

FetchCvdConfig ParseFetchCvdConfigs(Json::Value& root) {
  InitFetchCvdConfigs(root);
  return GenerateFetchCvdFlags(root);
}

}  // namespace cuttlefish

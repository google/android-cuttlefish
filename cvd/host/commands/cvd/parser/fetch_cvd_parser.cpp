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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {
namespace {

Result<void> InitFetchInstanceConfigs(Json::Value& instance) {
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"disk", "default_build"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"disk", "super", "system"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"boot", "kernel", "build"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"disk", "download_img_zip"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"disk", "download_target_files_zip"}));
  return {};
}

Result<void> InitFetchCvdConfigs(Json::Value& root) {
  CF_EXPECT(
      InitConfig(root, Json::Value::nullSingleton(), {"fetch", "api_key"}));
  CF_EXPECT(InitConfig(root, Json::Value::nullSingleton(),
                       {"fetch", "credential_source"}));
  CF_EXPECT(InitConfig(root, Json::Value::nullSingleton(),
                       {"fetch", "wait_retry_period"}));
  CF_EXPECT(InitConfig(root, Json::Value::nullSingleton(),
                       {"fetch", "external_dns_resolver"}));
  CF_EXPECT(InitConfig(root, Json::Value::nullSingleton(),
                       {"fetch", "keep_downloaded_archives"}));
  Json::Value& instances = root["instances"];
  const int size = instances.size();
  for (int i = 0; i < size; i++) {
    CF_EXPECT(InitFetchInstanceConfigs(instances[i]));
  }
  return {};
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
      .system_build = OptString(instance["disk"]["super"]["system"]),
      .kernel_build = OptString(instance["boot"]["kernel"]["build"]),
      .download_img_zip = OptString(instance["disk"]["download_img_zip"]),
      .download_target_files_zip =
          OptString(instance["disk"]["download_target_files_zip"])};
  result.should_fetch = ShouldFetch(
      {result.default_build, result.system_build, result.kernel_build});
  return result;
}

FetchCvdConfig ParseFetchConfigs(const Json::Value& root) {
  auto result = FetchCvdConfig{
      .api_key = OptString(root["fetch"]["api_key"]),
      .credential_source = OptString(root["fetch"]["credential_source"]),
      .wait_retry_period = OptString(root["fetch"]["wait_retry_period"]),
      .external_dns_resolver =
          OptString(root["fetch"]["external_dns_resolver"]),
      .keep_downloaded_archives =
          OptString(root["fetch"]["keep_downloaded_archives"])};

  const int num_instances = root["instances"].size();
  for (unsigned int i = 0; i < num_instances; i++) {
    result.instances.emplace_back(
        ParseFetchInstanceConfigs(root["instances"][i]));
  }
  return result;
}

}  // namespace

Result<FetchCvdConfig> ParseFetchCvdConfigs(Json::Value& root) {
  CF_EXPECT(InitFetchCvdConfigs(root));
  return {ParseFetchConfigs(root)};
}

}  // namespace cuttlefish

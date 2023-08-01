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
#include <string_view>
#include <vector>

#include <android-base/strings.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kFetchPrefix = "@ab/";

Result<void> InitFetchInstanceConfigs(Json::Value& instance) {
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"disk", "default_build"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"disk", "super", "system"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"boot", "kernel", "build"}));
  CF_EXPECT(
      InitConfig(instance, Json::Value::nullSingleton(), {"boot", "build"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"boot", "bootloader", "build"}));
  CF_EXPECT(
      InitConfig(instance, Json::Value::nullSingleton(), {"disk", "otatools"}));
  CF_EXPECT(InitConfig(instance, Json::Value::nullSingleton(),
                       {"disk", "host_package"}));
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

std::optional<std::string> GetOptString(const Json::Value& value) {
  if (value.isNull()) {
    return std::nullopt;
  }
  return value.asString();
}

std::optional<std::string> GetRemoteBuildString(const Json::Value& value) {
  if (value.isNull()) {
    return std::nullopt;
  }
  std::string strVal = value.asString();
  std::string_view result = strVal;
  if (!android::base::ConsumePrefix(&result, kFetchPrefix)) {
    return std::nullopt;
  }
  return std::string(result);
}

bool ShouldFetch(const std::vector<std::optional<std::string>>& values) {
  return std::any_of(std::begin(values), std::end(values),
                     [](const std::optional<std::string>& value) {
                       return value.has_value();
                     });
}

FetchCvdInstanceConfig ParseFetchInstanceConfigs(const Json::Value& instance) {
  auto result = FetchCvdInstanceConfig{
      .default_build = GetRemoteBuildString(instance["disk"]["default_build"]),
      .system_build = GetRemoteBuildString(instance["disk"]["super"]["system"]),
      .kernel_build = GetRemoteBuildString(instance["boot"]["kernel"]["build"]),
      .boot_build = GetRemoteBuildString(instance["boot"]["build"]),
      .bootloader_build =
          GetRemoteBuildString(instance["boot"]["bootloader"]["build"]),
      .otatools_build = GetRemoteBuildString(instance["disk"]["otatools"]),
      .host_package_build =
          GetRemoteBuildString(instance["disk"]["host_package"]),
      .download_img_zip = GetOptString(instance["disk"]["download_img_zip"]),
      .download_target_files_zip =
          GetOptString(instance["disk"]["download_target_files_zip"])};
  result.should_fetch = ShouldFetch(
      {result.default_build, result.system_build, result.kernel_build,
       result.boot_build, result.bootloader_build, result.otatools_build,
       result.host_package_build});
  return result;
}

FetchCvdConfig ParseFetchConfigs(const Json::Value& root) {
  auto result = FetchCvdConfig{
      .api_key = GetOptString(root["fetch"]["api_key"]),
      .credential_source = GetOptString(root["fetch"]["credential_source"]),
      .wait_retry_period = GetOptString(root["fetch"]["wait_retry_period"]),
      .external_dns_resolver =
          GetOptString(root["fetch"]["external_dns_resolver"]),
      .keep_downloaded_archives =
          GetOptString(root["fetch"]["keep_downloaded_archives"])};

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

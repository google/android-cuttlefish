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

struct FetchCvdInstanceConfig {
  bool should_fetch = false;
  std::optional<std::string> default_build;
  std::optional<std::string> system_build;
  std::optional<std::string> kernel_build;
  std::optional<std::string> boot_build;
  std::optional<std::string> bootloader_build;
  std::optional<std::string> otatools_build;
  std::optional<std::string> host_package_build;
  std::optional<std::string> download_img_zip;
  std::optional<std::string> download_target_files_zip;
};

struct FetchCvdConfig {
  std::optional<std::string> api_key;
  std::optional<std::string> credential_source;
  std::optional<std::string> wait_retry_period;
  std::optional<std::string> external_dns_resolver;
  std::optional<std::string> keep_downloaded_archives;
  std::vector<FetchCvdInstanceConfig> instances;
};

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
  for (auto& instance : root["instances"]) {
    CF_EXPECT(InitFetchInstanceConfigs(instance));
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

  for (const auto& instance : root["instances"]) {
    result.instances.emplace_back(ParseFetchInstanceConfigs(instance));
  }
  return result;
}

std::optional<std::string> JoinBySelectorOptional(
    const std::vector<FetchCvdInstanceConfig>& collection,
    const std::function<std::string(const FetchCvdInstanceConfig&)>& selector) {
  std::vector<std::string> selected;
  selected.reserve(collection.size());
  for (const auto& instance : collection) {
    selected.emplace_back(selector(instance));
  }
  std::string result = android::base::Join(selected, ',');
  // no values, empty or only ',' separators
  if (result.size() == collection.size() - 1) {
    return std::nullopt;
  }
  return result;
}

std::vector<std::string> GenerateFetchFlags(
    const FetchCvdConfig& config,
    const std::vector<FetchCvdInstanceConfig>& fetch_instances,
    const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories) {
  std::vector<std::string> result;
  if (fetch_instances.empty()) {
    return result;
  }

  result.emplace_back("--target_directory=" + target_directory);
  if (config.api_key) {
    result.emplace_back("--api_key=" + *config.api_key);
  }
  if (config.credential_source) {
    result.emplace_back("--credential_source=" + *config.credential_source);
  }
  if (config.wait_retry_period) {
    result.emplace_back("--wait_retry_period=" + *config.wait_retry_period);
  }
  if (config.external_dns_resolver) {
    result.emplace_back("--external_dns_resolver=" +
                        *config.external_dns_resolver);
  }
  if (config.keep_downloaded_archives) {
    result.emplace_back("--keep_downloaded_archives=" +
                        *config.keep_downloaded_archives);
  }

  result.emplace_back("--target_subdirectory=" +
                      android::base::Join(target_subdirectories, ','));
  std::optional<std::string> default_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.default_build.value_or("");
      });
  if (default_build_params) {
    result.emplace_back("--default_build=" + *default_build_params);
  }
  std::optional<std::string> system_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.system_build.value_or("");
      });
  if (system_build_params) {
    result.emplace_back("--system_build=" + *system_build_params);
  }
  std::optional<std::string> kernel_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.kernel_build.value_or("");
      });
  if (kernel_build_params) {
    result.emplace_back("--kernel_build=" + *kernel_build_params);
  }
  std::optional<std::string> boot_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.boot_build.value_or("");
      });
  if (boot_build_params) {
    result.emplace_back("--boot_build=" + *boot_build_params);
  }
  std::optional<std::string> bootloader_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.bootloader_build.value_or("");
      });
  if (bootloader_build_params) {
    result.emplace_back("--bootloader_build=" + *bootloader_build_params);
  }
  std::optional<std::string> otatools_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.otatools_build.value_or("");
      });
  if (otatools_build_params) {
    result.emplace_back("--otatools_build=" + *otatools_build_params);
  }
  std::optional<std::string> host_package_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.host_package_build.value_or("");
      });
  if (host_package_build_params) {
    result.emplace_back("--host_package_build=" + *host_package_build_params);
  }
  std::optional<std::string> download_img_zip_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.download_img_zip.value_or("");
      });
  if (download_img_zip_params) {
    result.emplace_back("--download_img_zip=" + *download_img_zip_params);
  }
  std::optional<std::string> download_target_files_zip_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.download_target_files_zip.value_or("");
      });
  if (download_target_files_zip_params) {
    result.emplace_back("--download_target_files_zip=" +
                        *download_target_files_zip_params);
  }
  return result;
}

}  // namespace

Result<std::vector<std::string>> ParseFetchCvdConfigs(
    Json::Value& root, const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories) {
  CF_EXPECT(InitFetchCvdConfigs(root));
  auto fetch_configs = ParseFetchConfigs(root);

  std::vector<FetchCvdInstanceConfig> fetch_instances;
  for (const auto& instance : fetch_configs.instances) {
    if (instance.should_fetch) {
      fetch_instances.emplace_back(instance);
    }
  }
  return GenerateFetchFlags(fetch_configs, fetch_instances, target_directory,
                            target_subdirectories);
}

}  // namespace cuttlefish

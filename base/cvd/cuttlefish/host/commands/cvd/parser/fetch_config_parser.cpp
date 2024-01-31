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

#include "host/commands/cvd/parser/fetch_config_parser.h"

#include <string>
#include <string_view>
#include <vector>

#include <android-base/strings.h>
#include <json/json.h>

#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/libs/web/build_api.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kFetchPrefix = "@ab/";

Result<void> InitFetchInstanceConfigs(Json::Value& instance) {
  CF_EXPECT(
      InitConfig(instance, kDefaultBuildString, {"disk", "default_build"}));
  CF_EXPECT(
      InitConfig(instance, kDefaultBuildString, {"disk", "super", "system"}));
  CF_EXPECT(
      InitConfig(instance, kDefaultBuildString, {"boot", "kernel", "build"}));
  CF_EXPECT(InitConfig(instance, kDefaultBuildString, {"boot", "build"}));
  CF_EXPECT(InitConfig(instance, kDefaultBuildString,
                       {"boot", "bootloader", "build"}));
  CF_EXPECT(InitConfig(instance, kDefaultBuildString, {"disk", "otatools"}));
  CF_EXPECT(InitConfig(instance, kDefaultDownloadImgZip,
                       {"disk", "download_img_zip"}));
  CF_EXPECT(InitConfig(instance, kDefaultDownloadTargetFilesZip,
                       {"disk", "download_target_files_zip"}));
  return {};
}

Result<void> InitFetchCvdConfigs(Json::Value& root) {
  CF_EXPECT(InitConfig(root, kDefaultApiKey, {"fetch", "api_key"}));
  CF_EXPECT(InitConfig(root, kDefaultCredentialSource,
                       {"fetch", "credential_source"}));
  CF_EXPECT(InitConfig(root, static_cast<int>(kDefaultWaitRetryPeriod.count()),
                       {"fetch", "wait_retry_period"}));
  CF_EXPECT(InitConfig(root, kDefaultExternalDnsResolver,
                       {"fetch", "external_dns_resolver"}));
  CF_EXPECT(InitConfig(root, kDefaultKeepDownloadedArchives,
                       {"fetch", "keep_downloaded_archives"}));
  CF_EXPECT(
      InitConfig(root, kAndroidBuildServiceUrl, {"fetch", "api_base_url"}));
  CF_EXPECT(InitConfig(root, kDefaultBuildString, {"common", "host_package"}));
  for (auto& instance : root["instances"]) {
    CF_EXPECT(InitFetchInstanceConfigs(instance));
  }
  return {};
}

bool ShouldFetch(const Json::Value& instance) {
  for (const auto& value :
       {instance["disk"]["default_build"], instance["disk"]["super"]["system"],
        instance["boot"]["kernel"]["build"], instance["boot"]["build"],
        instance["boot"]["bootloader"]["build"],
        instance["disk"]["otatools"]}) {
    // expects non-prefixed build strings already converted to empty strings
    if (!value.asString().empty()) {
      return true;
    }
  }
  return false;
}

Result<std::string> GetFetchBuildString(const Json::Value& value) {
  std::string strVal = value.asString();
  std::string_view view = strVal;
  if (!android::base::ConsumePrefix(&view, kFetchPrefix)) {
    // intentionally return an empty string when there are local, non-prefixed
    // paths.  Fetch does not process the local paths
    return "";
  }
  CF_EXPECTF(!view.empty(),
             "\"{}\" prefixed build string was not followed by a value",
             kFetchPrefix);
  return std::string(view);
}

Result<Json::Value> RemoveNonPrefixedBuildStrings(const Json::Value& instance) {
  auto result = Json::Value(instance);
  result["disk"]["default_build"] =
      CF_EXPECT(GetFetchBuildString(result["disk"]["default_build"]));
  result["disk"]["super"]["system"] =
      CF_EXPECT(GetFetchBuildString(result["disk"]["super"]["system"]));
  result["boot"]["kernel"]["build"] =
      CF_EXPECT(GetFetchBuildString(result["boot"]["kernel"]["build"]));
  result["boot"]["build"] =
      CF_EXPECT(GetFetchBuildString(result["boot"]["build"]));
  result["boot"]["bootloader"]["build"] =
      CF_EXPECT(GetFetchBuildString(result["boot"]["bootloader"]["build"]));
  result["disk"]["otatools"] =
      CF_EXPECT(GetFetchBuildString(result["disk"]["otatools"]));
  return result;
}

Result<std::vector<std::string>> GenerateFetchFlags(
    const Json::Value& root, const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories) {
  Json::Value fetch_instances = Json::Value(Json::ValueType::arrayValue);
  std::vector<std::string> fetch_subdirectories;
  const auto& instances = root["instances"];
  CF_EXPECT_EQ(instances.size(), target_subdirectories.size(),
               "Mismatched sizes between number of subdirectories and number "
               "of instances");
  for (int i = 0; i < instances.size(); i++) {
    const auto prefix_filtered =
        CF_EXPECT(RemoveNonPrefixedBuildStrings(instances[i]));
    if (ShouldFetch(prefix_filtered)) {
      fetch_instances.append(prefix_filtered);
      fetch_subdirectories.emplace_back(target_subdirectories[i]);
    }
  }

  const std::string host_package_build =
      CF_EXPECT(GetFetchBuildString(root["common"]["host_package"]));
  std::vector<std::string> result;
  if (fetch_subdirectories.empty() && host_package_build.empty()) {
    return result;
  }

  result.emplace_back(GenerateGflag("target_directory", {target_directory}));
  result.emplace_back(GenerateGflag(
      "api_key",
      {CF_EXPECT(GetValue<std::string>(root, {"fetch", "api_key"}))}));
  result.emplace_back(GenerateGflag(
      "credential_source", {CF_EXPECT(GetValue<std::string>(
                               root, {"fetch", "credential_source"}))}));
  result.emplace_back(GenerateGflag(
      "wait_retry_period", {CF_EXPECT(GetValue<std::string>(
                               root, {"fetch", "wait_retry_period"}))}));
  result.emplace_back(
      GenerateGflag("external_dns_resolver",
                    {CF_EXPECT(GetValue<std::string>(
                        root, {"fetch", "external_dns_resolver"}))}));
  result.emplace_back(
      GenerateGflag("keep_downloaded_archives",
                    {CF_EXPECT(GetValue<std::string>(
                        root, {"fetch", "keep_downloaded_archives"}))}));
  result.emplace_back(GenerateGflag(
      "api_base_url",
      {CF_EXPECT(GetValue<std::string>(root, {"fetch", "api_base_url"}))}));
  result.emplace_back(
      GenerateGflag("host_package_build", {host_package_build}));

  result.emplace_back(
      GenerateGflag("target_subdirectory", fetch_subdirectories));
  result.emplace_back(CF_EXPECT(GenerateGflag(fetch_instances, "default_build",
                                              {"disk", "default_build"})));
  result.emplace_back(CF_EXPECT(GenerateGflag(fetch_instances, "system_build",
                                              {"disk", "super", "system"})));
  result.emplace_back(CF_EXPECT(GenerateGflag(fetch_instances, "kernel_build",
                                              {"boot", "kernel", "build"})));
  result.emplace_back(CF_EXPECT(
      GenerateGflag(fetch_instances, "boot_build", {"boot", "build"})));
  result.emplace_back(CF_EXPECT(GenerateGflag(
      fetch_instances, "bootloader_build", {"boot", "bootloader", "build"})));
  result.emplace_back(CF_EXPECT(
      GenerateGflag(fetch_instances, "otatools_build", {"disk", "otatools"})));
  result.emplace_back(CF_EXPECT(GenerateGflag(
      fetch_instances, "download_img_zip", {"disk", "download_img_zip"})));
  result.emplace_back(
      CF_EXPECT(GenerateGflag(fetch_instances, "download_target_files_zip",
                              {"disk", "download_target_files_zip"})));
  return result;
}

}  // namespace

Result<std::vector<std::string>> ParseFetchCvdConfigs(
    Json::Value& root, const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories) {
  CF_EXPECT(InitFetchCvdConfigs(root));
  return CF_EXPECT(
      GenerateFetchFlags(root, target_directory, target_subdirectories));
}

}  // namespace cuttlefish

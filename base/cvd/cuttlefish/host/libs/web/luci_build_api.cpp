//
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/libs/web/luci_build_api.h"

#include <memory>
#include <string_view>

#include <fmt/format.h>

#include "android-base/strings.h"
#include "common/libs/utils/json.h"
#include "host/libs/web/chrome_os_build_string.h"

namespace cuttlefish {

LuciBuildApi::LuciBuildApi() : http_client_(HttpClient::CurlClient()) {}

LuciBuildApi::LuciBuildApi(
    std::unique_ptr<HttpClient> http_client,
    std::unique_ptr<HttpClient> inner_http_client,
    std::unique_ptr<CredentialSource> buildbucket_credential_source,
    std::unique_ptr<CredentialSource> storage_credential_source)
    : http_client_(std::move(http_client)),
      inner_http_client_(std::move(inner_http_client)),
      buildbucket_credential_source_(std::move(buildbucket_credential_source)),
      storage_credential_source_(std::move(storage_credential_source)) {}

Result<std::vector<std::string>> LuciBuildApi::BuildBucketHeaders() {
  std::vector<std::string> headers;
  if (buildbucket_credential_source_) {
    std::string credential =
        CF_EXPECT(buildbucket_credential_source_->Credential());
    headers.emplace_back("Authorization: Bearer " + credential);
  }
  // Documented at https://pkg.go.dev/go.chromium.org/luci/grpc/prpc
  headers.emplace_back("Content-Type: application/json");  // Input format
  headers.emplace_back("Accept: application/json");        // Output format
  return headers;
}

Result<std::vector<std::string>> LuciBuildApi::CloudStorageHeaders() {
  std::vector<std::string> headers;
  if (storage_credential_source_) {
    std::string credential =
        CF_EXPECT(storage_credential_source_->Credential());
    headers.emplace_back("Authorization: Bearer " + credential);
  }
  return headers;
}

Result<std::optional<ChromeOsBuildArtifacts>> LuciBuildApi::GetBuildArtifacts(
    const ChromeOsBuildString& build_string) {
  Json::Value request;
  request["mask"]["fields"] = "output.properties";
  request["pageSize"] = 1;
  auto& predicate = request["predicate"];
  predicate["status"] = "SUCCESS";
  if (std::holds_alternative<std::string>(build_string)) {
    const auto& id = std::get<std::string>(build_string);
    auto& build = predicate["build"];
    build["startBuildId"] = id;
    build["endBuildId"] = id;
  } else if (std::holds_alternative<ChromeOsBuilder>(build_string)) {
    const auto& builder = std::get<ChromeOsBuilder>(build_string);
    auto& json_builder = predicate["builder"];
    json_builder["project"] = builder.project;
    json_builder["bucket"] = builder.bucket;
    json_builder["builder"] = builder.builder;
  }

  static constexpr std::string_view kBuildbucketUrl =
      "https://cr-buildbucket.appspot.com/prpc/buildbucket.v2.Builds";
  auto url = fmt::format("{}/SearchBuilds?format=json", kBuildbucketUrl);

  std::stringstream json_str;
  json_str << request;
  auto headers = CF_EXPECT(BuildBucketHeaders());
  auto response =
      CF_EXPECT(http_client_->PostToString(url, json_str.str(), headers));
  if (!response.HttpSuccess()) {
    return {};
  }
  constexpr std::string_view kPreventXssiPrefix = ")]}'\n";
  std::string_view response_data = response.data;
  CF_EXPECT(android::base::ConsumePrefix(&response_data, kPreventXssiPrefix));
  auto response_json = CF_EXPECT(ParseJson(response_data));

  ChromeOsBuildArtifacts chrome_os_build;

  CF_EXPECT(response_json.isMember("builds"));
  auto& builds = response_json["builds"];
  CF_EXPECT_EQ(builds.type(), Json::ValueType::arrayValue);
  CF_EXPECT(!builds.empty());
  auto& first_build = builds[0];
  CF_EXPECT(first_build.isMember("output"));
  auto& output = first_build["output"];
  CF_EXPECT(output.isMember("properties"));
  auto& properties = output["properties"];

  CF_EXPECT(properties.isMember("artifact_link"));
  chrome_os_build.artifact_link = properties["artifact_link"].asString();

  CF_EXPECT(properties.isMember("artifacts"));
  auto& artifacts = properties["artifacts"];
  CF_EXPECT(artifacts.isMember("files_by_artifact"));
  auto& files_by_artifact = artifacts["files_by_artifact"];

  for (const auto& artifact : files_by_artifact) {
    for (const auto& file : artifact) {
      chrome_os_build.artifact_files.emplace_back(file.asString());
    }
  }

  return chrome_os_build;
}

Result<void> LuciBuildApi::DownloadArtifact(const std::string& artifact_link,
                                            const std::string& artifact_file,
                                            const std::string& target_path) {
  std::string_view trim_link = artifact_link;
  CF_EXPECT(android::base::ConsumePrefix(&trim_link, "gs://"));
  auto path_fragments = android::base::Split(std::string(trim_link), "/");
  CF_EXPECT(!path_fragments.empty());
  auto bucket = path_fragments[0];
  path_fragments.erase(path_fragments.begin());
  path_fragments.emplace_back(artifact_file);
  auto object = fmt::format("{}", fmt::join(path_fragments, "/"));

  auto url = fmt::format(
      "https://storage.googleapis.com/storage/v1/b/{}/o/{}?alt=media",
      http_client_->UrlEscape(bucket), http_client_->UrlEscape(object));

  auto headers = CF_EXPECT(CloudStorageHeaders());
  CF_EXPECT(http_client_->DownloadToFile(url, target_path, headers));
  return {};
}

}  // namespace cuttlefish

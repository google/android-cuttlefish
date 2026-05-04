/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/metrics/gce_environment.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include "absl/strings/str_split.h"

#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"
#include "cuttlefish/host/libs/web/http_client/curl_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_string.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kDetectUrl = "metadata.google.internal";
constexpr std::string_view kDetectHeader = "Metadata-Flavor";
constexpr std::string_view kExpectedDetectHeaderValue = "Google";

// https://docs.cloud.google.com/compute/docs/metadata/predefined-metadata-keys
constexpr std::string_view kGceMetadataUrlBase =
    "http://metadata.google.internal/computeMetadata/v1";
constexpr std::string_view kNumericProjectId = "project/numeric-project-id";
constexpr std::string_view kZone = "instance/zone";

// https://docs.cloud.google.com/compute/docs/instances/detect-compute-engine
Result<bool> IsGceEnvironment(HttpClient& http_client) {
  const HttpResponse<std::string> response =
      CF_EXPECT(HttpGetToString(http_client, std::string(kDetectUrl)));
  CF_EXPECTF(response.HttpSuccess(),
             "GCE environment detection received a failure response ({}: {}), "
             "data:  {}",
             response.http_code, response.StatusDescription(), response.data);
  return HeaderValue(response.headers, kDetectHeader) ==
         kExpectedDetectHeaderValue;
}

// https://docs.cloud.google.com/compute/docs/metadata/querying-metadata
Result<std::string> QueryGceMetadata(HttpClient& http_client,
                                     std::string_view metadata_key) {
  const std::vector<std::string> required_metadata_query_header = {
      fmt::format("{}: {}", kDetectHeader, kExpectedDetectHeaderValue)};
  const std::string url =
      fmt::format("{}/{}", kGceMetadataUrlBase, metadata_key);
  HttpResponse<std::string> response = CF_EXPECT(
      HttpGetToString(http_client, url, required_metadata_query_header));
  CF_EXPECTF(response.HttpSuccess(),
             "GCE query received a failure response ({}: {}), data:  {}",
             response.http_code, response.StatusDescription(), response.data);
  return response.data;
}

Result<std::string> GetZoneValue(std::string_view response_string) {
  std::vector<std::string> components = absl::StrSplit(response_string, "/");
  CF_EXPECTF(
      components.size() == 4 && components[0] == "projects" &&
          components[2] == "zones",
      "Unexpected format in response string for zone metadata, expected "
      "the form \"projects/<project_num>/zones/<zone>\" and received: {}",
      response_string);
  return components[3];
}

}  // namespace

Result<std::optional<GceEnvironment>> DetectGceEnvironment() {
  CurlGlobalInit curl_init;
  const bool use_logging_debug_function = true;
  std::unique_ptr<HttpClient> http_client =
      CurlHttpClient(use_logging_debug_function);
  if (!CF_EXPECT(IsGceEnvironment(*http_client))) {
    return std::nullopt;
  }
  const std::string zone_response =
      CF_EXPECT(QueryGceMetadata(*http_client, kZone));
  return GceEnvironment{
      .numeric_project_id =
          CF_EXPECT(QueryGceMetadata(*http_client, kNumericProjectId)),
      .zone = CF_EXPECT(GetZoneValue(zone_response)),
  };
}

}  // namespace cuttlefish

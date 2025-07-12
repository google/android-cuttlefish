//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/android_build_url.h"

#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "cuttlefish/host/libs/web/http_client/url_escape.h"

namespace cuttlefish {

namespace {

struct UrlBuilder {
  std::string base_url;
  std::vector<std::string> query_string;

  void AddQueryParameter(std::string_view key, std::string_view value) {
    query_string.push_back(fmt::format("{}={}", key, UrlEscape(value)));
  }

  void AddApiKeyAndProjectId(std::string_view api_key,
                             std::string_view project_id) {
    if (!api_key.empty()) {
      AddQueryParameter("key", api_key);
    }
    if (!project_id.empty()) {
      AddQueryParameter("$userProject", project_id);
    }
  }

  std::string GetUrl() {
    std::stringstream result;
    result << base_url;
    if (!query_string.empty()) {
      fmt::print(result, "?{}", fmt::join(query_string, "&"));
    }
    return result.str();
  }
};

std::string BuildNameRegexp(
    const std::vector<std::string>& artifact_filenames) {
  // surrounding with \Q and \E treats the text literally to avoid
  // characters being treated as regex
  auto it = artifact_filenames.begin();
  std::string name_regex = "^\\Q" + *it + "\\E$";
  std::string result = name_regex;
  ++it;
  for (const auto end = artifact_filenames.end(); it != end; ++it) {
    name_regex = "^\\Q" + *it + "\\E$";
    result += "|" + name_regex;
  }
  return result;
}

UrlBuilder GetLatestBuildIdBaseUrl(std::string_view api_base) {
  return UrlBuilder{.base_url = fmt::format("{}/builds", api_base)};
}

UrlBuilder GetBuildStatusBaseUrl(std::string_view api_base, std::string_view id,
                                 std::string_view target) {
  return UrlBuilder{.base_url = fmt::format("{}/builds/{}/{}", api_base,
                                            UrlEscape(id), UrlEscape(target))};
}

UrlBuilder GetProductNameBaseUrl(std::string_view api_base, std::string_view id,
                                 std::string_view target) {
  return UrlBuilder{.base_url = fmt::format("{}/builds/{}/{}", api_base,
                                            UrlEscape(id), UrlEscape(target))};
}

UrlBuilder GetArtifactBaseUrl(std::string_view api_base, std::string_view id,
                              std::string_view target) {
  return UrlBuilder{
      .base_url = fmt::format("{}/builds/{}/{}/attempts/latest/artifacts",
                              api_base, UrlEscape(id), UrlEscape(target))};
}

UrlBuilder GetArtifactDownloadBaseUrl(std::string_view api_base,
                                      std::string_view id,
                                      std::string_view target,
                                      std::string_view artifact) {
  return UrlBuilder{.base_url = fmt::format(
                        "{}/builds/{}/{}/attempts/latest/artifacts/{}/url",
                        api_base, UrlEscape(id), UrlEscape(target),
                        UrlEscape(artifact))};
}

}  // namespace

AndroidBuildUrl::AndroidBuildUrl(std::string api_base_url, std::string api_key,
                                 std::string project_id)
    : api_base_url_(std::move(api_base_url)),
      api_key_(std::move(api_key)),
      project_id_(std::move(project_id)) {}

std::string AndroidBuildUrl::GetLatestBuildIdUrl(std::string_view branch,
                                                 std::string_view target) {
  UrlBuilder builder = GetLatestBuildIdBaseUrl(api_base_url_);
  builder.AddQueryParameter("buildAttemptStatus", "complete");
  builder.AddQueryParameter("buildType", "submitted");
  builder.AddQueryParameter("maxResults", "1");
  builder.AddQueryParameter("successful", "true");
  builder.AddQueryParameter("branch", branch);
  builder.AddQueryParameter("target", target);
  builder.AddApiKeyAndProjectId(api_key_, project_id_);

  return builder.GetUrl();
}

std::string AndroidBuildUrl::GetBuildStatusUrl(std::string_view id,
                                               std::string_view target) {
  UrlBuilder builder = GetBuildStatusBaseUrl(api_base_url_, id, target);
  builder.AddApiKeyAndProjectId(api_key_, project_id_);

  return builder.GetUrl();
}

std::string AndroidBuildUrl::GetProductNameUrl(std::string_view id,
                                               std::string_view target) {
  UrlBuilder builder = GetProductNameBaseUrl(api_base_url_, id, target);
  builder.AddApiKeyAndProjectId(api_key_, project_id_);

  return builder.GetUrl();
}

std::string AndroidBuildUrl::GetArtifactUrl(
    std::string_view id, std::string_view target,
    const std::vector<std::string>& artifact_filenames,
    std::string_view page_token) {
  UrlBuilder builder = GetArtifactBaseUrl(api_base_url_, id, target);
  builder.AddQueryParameter("maxResults", "100");
  if (!artifact_filenames.empty()) {
    builder.AddQueryParameter("nameRegexp",
                              BuildNameRegexp(artifact_filenames));
  }
  if (!page_token.empty()) {
    builder.AddQueryParameter("pageToken", page_token);
  }
  builder.AddApiKeyAndProjectId(api_key_, project_id_);

  return builder.GetUrl();
}

std::string AndroidBuildUrl::GetArtifactDownloadUrl(std::string_view id,
                                                    std::string_view target,
                                                    std::string_view artifact) {
  UrlBuilder builder =
      GetArtifactDownloadBaseUrl(api_base_url_, id, target, artifact);
  builder.AddApiKeyAndProjectId(api_key_, project_id_);

  return builder.GetUrl();
}

}  // namespace cuttlefish

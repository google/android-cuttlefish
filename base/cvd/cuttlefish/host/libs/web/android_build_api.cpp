//
// Copyright (C) 2019 The Android Open Source Project
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

#include "host/libs/web/android_build_api.h"

#include <dirent.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/web/android_build_string.h"
#include "host/libs/web/credential_source.h"

namespace cuttlefish {
namespace {

bool StatusIsTerminal(const std::string& status) {
  const static std::set<std::string> terminal_statuses = {
      "abandoned", "complete", "error", "ABANDONED", "COMPLETE", "ERROR",
  };
  return terminal_statuses.count(status) > 0;
}

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

struct CloseDir {
  void operator()(DIR* dir) { closedir(dir); }
};

}  // namespace

DeviceBuild::DeviceBuild(std::string id, std::string target,
                         std::optional<std::string> filepath)
    : id(std::move(id)),
      target(std::move(target)),
      filepath(std::move(filepath)) {}

std::ostream& operator<<(std::ostream& out, const DeviceBuild& build) {
  return out << "(id=\"" << build.id << "\", target=\"" << build.target
             << "\", filepath=\"" << build.filepath.value_or("") << "\")";
}

DirectoryBuild::DirectoryBuild(std::vector<std::string> paths,
                               std::string target,
                               std::optional<std::string> filepath)
    : paths(std::move(paths)),
      target(std::move(target)),
      id("eng"),
      filepath(std::move(filepath)) {
  product = StringFromEnv("TARGET_PRODUCT", "");
}

std::ostream& operator<<(std::ostream& out, const DirectoryBuild& build) {
  auto paths = android::base::Join(build.paths, ":");
  return out << "(paths=\"" << paths << "\", target=\"" << build.target
             << "\", filepath=\"" << build.filepath.value_or("") << "\")";
}

std::ostream& operator<<(std::ostream& out, const Build& build) {
  std::visit([&out](auto&& arg) { out << arg; }, build);
  return out;
}

BuildApi::BuildApi(std::unique_ptr<HttpClient> http_client,
                   std::unique_ptr<HttpClient> inner_http_client,
                   std::unique_ptr<CredentialSource> credential_source,
                   std::string api_key, const std::chrono::seconds retry_period,
                   std::string api_base_url, std::string project_id)
    : http_client(std::move(http_client)),
      inner_http_client(std::move(inner_http_client)),
      credential_source(std::move(credential_source)),
      api_key_(std::move(api_key)),
      retry_period_(retry_period),
      api_base_url_(std::move(api_base_url)),
      project_id_(std::move(project_id)) {}

Result<Build> BuildApi::GetBuild(const DeviceBuildString& build_string,
                                 const std::string& fallback_target) {
  auto proposed_build = DeviceBuild(
      build_string.branch_or_id, build_string.target.value_or(fallback_target),
      build_string.filepath);
  auto latest_build_id =
      CF_EXPECT(LatestBuildId(build_string.branch_or_id,
                              build_string.target.value_or(fallback_target)));
  if (latest_build_id) {
    proposed_build.id = *latest_build_id;
    LOG(INFO) << "Latest build id for branch '" << build_string.branch_or_id
              << "' and target '" << proposed_build.target << "' is '"
              << proposed_build.id << "'";
  }

  std::string status = CF_EXPECT(BuildStatus(proposed_build));
  CF_EXPECT(status != "",
            proposed_build << " is not a valid branch or build id.");
  LOG(DEBUG) << "Status for build " << proposed_build << " is " << status;
  while (retry_period_ != std::chrono::seconds::zero() &&
         !StatusIsTerminal(status)) {
    LOG(DEBUG) << "Status is \"" << status << "\". Waiting for "
              << retry_period_.count() << " seconds.";
    std::this_thread::sleep_for(retry_period_);
    status = CF_EXPECT(BuildStatus(proposed_build));
  }
  LOG(DEBUG) << "Status for build " << proposed_build << " is " << status;
  proposed_build.product = CF_EXPECT(ProductName(proposed_build));
  return proposed_build;
}

Result<Build> BuildApi::GetBuild(const DirectoryBuildString& build_string,
                                 const std::string&) {
  return DirectoryBuild(build_string.paths, build_string.target,
                        build_string.filepath);
}

Result<Build> BuildApi::GetBuild(const BuildString& build_string,
                                 const std::string& fallback_target) {
  auto result =
      std::visit([this, &fallback_target](
                     auto&& arg) { return GetBuild(arg, fallback_target); },
                 build_string);
  return CF_EXPECT(std::move(result));
}

Result<std::string> BuildApi::DownloadFile(const Build& build,
                                           const std::string& target_directory,
                                           const std::string& artifact_name) {
  std::unordered_set<std::string> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name}));
  CF_EXPECT(Contains(artifacts, artifact_name),
            "Target " << build << " did not contain " << artifact_name);
  return DownloadTargetFile(build, target_directory, artifact_name);
}

Result<std::string> BuildApi::DownloadFileWithBackup(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name, const std::string& backup_artifact_name) {
  std::unordered_set<std::string> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name, backup_artifact_name}));
  std::string selected_artifact = artifact_name;
  if (!Contains(artifacts, artifact_name)) {
    selected_artifact = backup_artifact_name;
  }
  return DownloadTargetFile(build, target_directory, selected_artifact);
}

Result<std::vector<std::string>> BuildApi::Headers() {
  std::vector<std::string> headers;
  if (credential_source) {
    headers.push_back("Authorization: Bearer " +
                      CF_EXPECT(credential_source->Credential()));
  }
  return headers;
}

Result<std::optional<std::string>> BuildApi::LatestBuildId(
    const std::string& branch, const std::string& target) {
  std::string url =
      api_base_url_ + "/builds?branch=" + http_client->UrlEscape(branch) +
      "&buildAttemptStatus=complete" +
      "&buildType=submitted&maxResults=1&successful=true&target=" +
      http_client->UrlEscape(target);
  if (!api_key_.empty()) {
    url += "&key=" + http_client->UrlEscape(api_key_);
  }
  if(!project_id_.empty()){
    url += "&$userProject=" + http_client->UrlEscape(project_id_);
  }
  auto response =
      CF_EXPECT(http_client->DownloadToJson(url, CF_EXPECT(Headers())));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess(), "Error fetching the latest build of \""
                                        << target << "\" on \"" << branch
                                        << "\". The server response was \""
                                        << json << "\", and code was "
                                        << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  if (!json.isMember("builds")) {
    return std::nullopt;
  }
  CF_EXPECT(json["builds"].size() == 1,
            "Expected to receive 1 build for \""
                << target << "\" on \"" << branch << "\", but received "
                << json["builds"].size() << ". Full response:\n"
                << json);
  CF_EXPECT(json["builds"][0].isMember("buildId"),
            "\"buildId\" member missing from response.  Full response:\n"
                << json);
  return json["builds"][0]["buildId"].asString();
}

Result<std::string> BuildApi::BuildStatus(const DeviceBuild& build) {
  std::string url = api_base_url_ + "/builds/" +
                    http_client->UrlEscape(build.id) + "/" +
                    http_client->UrlEscape(build.target);
  std::vector<std::string> params;
  if (!api_key_.empty()) {
    params.push_back("key=" + http_client->UrlEscape(api_key_));
  }
  if(!project_id_.empty()){
    params.push_back("$userProject=" + http_client->UrlEscape(project_id_));
  }
  if (!params.empty()) {
    url += "?" + android::base::Join(params, "&");
  }
  auto response =
      CF_EXPECT(http_client->DownloadToJson(url, CF_EXPECT(Headers())));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess(),
            "Error fetching the status of \""
                << build << "\". The server response was \"" << json
                << "\", and code was " << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  return json["buildAttemptStatus"].asString();
}

Result<std::string> BuildApi::ProductName(const DeviceBuild& build) {
  std::string url = api_base_url_ + "/builds/" +
                    http_client->UrlEscape(build.id) + "/" +
                    http_client->UrlEscape(build.target);
  std::vector<std::string> params;
  if (!api_key_.empty()) {
    params.push_back("key=" + http_client->UrlEscape(api_key_));
  }
  if(!project_id_.empty()){
    params.push_back("$userProject=" + http_client->UrlEscape(project_id_));
  }
  if (!params.empty()) {
    url += "?" + android::base::Join(params, "&");
  }
  auto response =
      CF_EXPECT(http_client->DownloadToJson(url, CF_EXPECT(Headers())));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess(),
            "Error fetching the product name of \""
                << build << "\". The server response was \"" << json
                << "\", and code was " << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  CF_EXPECT(json.isMember("target"), "Build was missing target field.");
  return json["target"]["product"].asString();
}

Result<std::unordered_set<std::string>> BuildApi::Artifacts(
    const DeviceBuild& build,
    const std::vector<std::string>& artifact_filenames) {
  std::string page_token = "";
  std::unordered_set<std::string> artifacts;
  do {
    std::string url = api_base_url_ + "/builds/" +
                      http_client->UrlEscape(build.id) + "/" +
                      http_client->UrlEscape(build.target) +
                      "/attempts/latest/artifacts?maxResults=100";
    if (!artifact_filenames.empty()) {
      url += "&nameRegexp=" +
             http_client->UrlEscape(BuildNameRegexp(artifact_filenames));
    }
    if (page_token != "") {
      url += "&pageToken=" + http_client->UrlEscape(page_token);
    }
    if (!api_key_.empty()) {
      url += "&key=" + http_client->UrlEscape(api_key_);
    }
    if(!project_id_.empty()){
      url += "&$userProject=" + http_client->UrlEscape(project_id_);
    }
    auto response =
        CF_EXPECT(http_client->DownloadToJson(url, CF_EXPECT(Headers())));
    const auto& json = response.data;
    CF_EXPECT(response.HttpSuccess(),
              "Error fetching the artifacts of \""
                  << build << "\". The server response was \"" << json
                  << "\", and code was " << response.http_code);
    CF_EXPECT(!json.isMember("error"),
              "Response had \"error\" but had http success status. Received \""
                  << json << "\"");
    if (json.isMember("nextPageToken")) {
      page_token = json["nextPageToken"].asString();
    } else {
      page_token = "";
    }
    for (const auto& artifact_json : json["artifacts"]) {
      artifacts.emplace(artifact_json["name"].asString());
    }
  } while (page_token != "");
  return artifacts;
}

Result<std::unordered_set<std::string>> BuildApi::Artifacts(
    const DirectoryBuild& build, const std::vector<std::string>&) {
  std::unordered_set<std::string> artifacts;
  for (const auto& path : build.paths) {
    auto dir = std::unique_ptr<DIR, CloseDir>(opendir(path.c_str()));
    CF_EXPECT(dir != nullptr, "Could not read files from \"" << path << "\"");
    for (auto entity = readdir(dir.get()); entity != nullptr;
         entity = readdir(dir.get())) {
      artifacts.emplace(std::string(entity->d_name));
    }
  }
  return artifacts;
}

Result<std::unordered_set<std::string>> BuildApi::Artifacts(
    const Build& build, const std::vector<std::string>& artifact_filenames) {
  auto res =
      std::visit([this, &artifact_filenames](
                     auto&& arg) { return Artifacts(arg, artifact_filenames); },
                 build);
  return CF_EXPECT(std::move(res));
}

Result<std::string> BuildApi::GetArtifactDownloadUrl(
    const DeviceBuild& build, const std::string& artifact) {
  std::string download_url_endpoint =
      api_base_url_ + "/builds/" + http_client->UrlEscape(build.id) + "/" +
      http_client->UrlEscape(build.target) + "/attempts/latest/artifacts/" +
      http_client->UrlEscape(artifact) + "/url";
  std::vector<std::string> params;
  if (!api_key_.empty()) {
    params.push_back("key=" + http_client->UrlEscape(api_key_));
  }
  if(!project_id_.empty()){
    params.push_back("$userProject=" + http_client->UrlEscape(project_id_));
  }
  if (!params.empty()) {
    download_url_endpoint += "?" + android::base::Join(params, "&");
  }
  auto response = CF_EXPECT(
      http_client->DownloadToJson(download_url_endpoint, CF_EXPECT(Headers())));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess() || response.HttpRedirect(),
            "Error fetching the url of \"" << artifact << "\" for \"" << build
                                           << "\". The server response was \""
                                           << json << "\", and code was "
                                           << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. "
                << "Received \"" << json << "\"");
  CF_EXPECT(json.isMember("signedUrl"),
            "URL endpoint did not have json path: " << json);
  return json["signedUrl"].asString();
}

Result<void> BuildApi::ArtifactToFile(const DeviceBuild& build,
                                      const std::string& artifact,
                                      const std::string& path) {
  const auto url = CF_EXPECT(GetArtifactDownloadUrl(build, artifact));
  bool is_successful_download =
      CF_EXPECT(http_client->DownloadToFile(url, path)).HttpSuccess();
  CF_EXPECT_EQ(is_successful_download, true);
  return {};
}

Result<void> BuildApi::ArtifactToFile(const DirectoryBuild& build,
                                      const std::string& artifact,
                                      const std::string& destination) {
  for (const auto& path : build.paths) {
    auto source = path + "/" + artifact;
    if (!FileExists(source)) {
      continue;
    }
    unlink(destination.c_str());
    CF_EXPECT(symlink(source.c_str(), destination.c_str()) == 0,
              "Could not create symlink from " << source << " to "
                                               << destination << ": "
                                               << strerror(errno));
    return {};
  }
  return CF_ERR("Could not find artifact \"" << artifact << "\" in build \""
                                             << build << "\"");
}

Result<void> BuildApi::ArtifactToFile(const Build& build,
                                      const std::string& artifact,
                                      const std::string& path) {
  auto res = std::visit(
      [this, &artifact, &path](auto&& arg) {
        return ArtifactToFile(arg, artifact, path);
      },
      build);
  CF_EXPECT(std::move(res));
  return {};
}

Result<std::string> BuildApi::DownloadTargetFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::string target_filepath =
      ConstructTargetFilepath(target_directory, artifact_name);
  CF_EXPECT(ArtifactToFile(build, artifact_name, target_filepath),
            "Unable to download " << build << ":" << artifact_name << " to "
                                  << target_filepath);
  return {target_filepath};
}

/** Returns the name of one of the artifact target zip files.
 *
 * For example, for a target "aosp_cf_x86_phone-userdebug" at a build "5824130",
 * the image zip file would be "aosp_cf_x86_phone-img-5824130.zip"
 */
Result<std::string> BuildApi::GetBuildZipName(const Build& build, const std::string& name) {
  std::string product =
      std::visit([](auto&& arg) { return arg.product; }, build);
  auto id = std::visit([](auto&& arg) { return arg.id; }, build);
  return product + "-" + name + "-" + id + ".zip";
}

std::tuple<std::string, std::string> GetBuildIdAndTarget(const Build& build) {
  auto id = std::visit([](auto&& arg) { return arg.id; }, build);
  auto target = std::visit([](auto&& arg) { return arg.target; }, build);
  return {id, target};
}

std::optional<std::string> GetFilepath(const Build& build) {
  return std::visit([](auto&& arg) { return arg.filepath; }, build);
}

std::string ConstructTargetFilepath(const std::string& directory,
                                    const std::string& filename) {
  return directory + "/" + filename;
}

}  // namespace cuttlefish

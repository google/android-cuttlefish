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

#include "cuttlefish/host/libs/web/android_build_api.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <optional>
#include <ostream>
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
#include <json/value.h>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/cas/cas_downloader.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"

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

AndroidBuildApi::AndroidBuildApi(HttpClient& http_client,
                                 CredentialSource* credential_source,
                                 std::string api_key,
                                 const std::chrono::seconds retry_period,
                                 std::string api_base_url,
                                 std::string project_id,
                                 CasDownloader* cas_downloader)
    : http_client(http_client),
      credential_source(credential_source),
      api_key_(std::move(api_key)),
      retry_period_(retry_period),
      api_base_url_(std::move(api_base_url)),
      project_id_(std::move(project_id)),
      cas_downloader_(cas_downloader) {}

Result<Build> AndroidBuildApi::GetBuild(const DeviceBuildString& build_string) {
  CF_EXPECT(build_string.target.has_value());
  DeviceBuild proposed_build = DeviceBuild(
      build_string.branch_or_id, *build_string.target, build_string.filepath);
  auto latest_build_id =
      CF_EXPECT(LatestBuildId(build_string.branch_or_id, *build_string.target));
  if (latest_build_id) {
    proposed_build.id = *latest_build_id;
    LOG(INFO) << "Latest build id for branch '" << build_string.branch_or_id
              << "' and target '" << proposed_build.target << "' is '"
              << proposed_build.id << "'";
  }

  std::string status = CF_EXPECT(BuildStatus(proposed_build));
  CF_EXPECT(!status.empty(),
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

Result<Build> AndroidBuildApi::GetBuild(
    const DirectoryBuildString& build_string) {
  return DirectoryBuild(build_string.paths, build_string.target,
                        build_string.filepath);
}

Result<Build> AndroidBuildApi::GetBuild(const BuildString& build_string) {
  Result<Build> result =
      std::visit([this](auto&& arg) { return GetBuild(arg); }, build_string);
  return CF_EXPECT(std::move(result));
}

Result<std::string> AndroidBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::unordered_set<std::string> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name}));
  CF_EXPECT(Contains(artifacts, artifact_name),
            "Target " << build << " did not contain " << artifact_name);
  return DownloadTargetFile(build, target_directory, artifact_name);
}

Result<std::string> AndroidBuildApi::DownloadFileWithBackup(
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

Result<std::vector<std::string>> AndroidBuildApi::Headers() {
  std::vector<std::string> headers;
  if (credential_source) {
    headers.push_back("Authorization: Bearer " +
                      CF_EXPECT(credential_source->Credential()));
  }
  return headers;
}

Result<std::optional<std::string>> AndroidBuildApi::LatestBuildId(
    const std::string& branch, const std::string& target) {
  std::string url =
      api_base_url_ + "/builds?branch=" + UrlEscape(branch) +
      "&buildAttemptStatus=complete" +
      "&buildType=submitted&maxResults=1&successful=true&target=" +
      UrlEscape(target);
  if (!api_key_.empty()) {
    url += "&key=" + UrlEscape(api_key_);
  }
  if(!project_id_.empty()){
    url += "&$userProject=" + UrlEscape(project_id_);
  }
  auto response =
      CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));
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
  return CF_EXPECT(GetValue<std::string>(json["builds"][0], { "buildId" }));
}

Result<std::string> AndroidBuildApi::BuildStatus(const DeviceBuild& build) {
  std::string url = api_base_url_ + "/builds/" + UrlEscape(build.id) + "/" +
                    UrlEscape(build.target);
  std::vector<std::string> params;
  if (!api_key_.empty()) {
    params.push_back("key=" + UrlEscape(api_key_));
  }
  if(!project_id_.empty()){
    params.push_back("$userProject=" + UrlEscape(project_id_));
  }
  if (!params.empty()) {
    url += "?" + android::base::Join(params, "&");
  }
  auto response =
      CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess(),
            "Error fetching the status of \""
                << build << "\". The server response was \"" << json
                << "\", and code was " << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  return CF_EXPECT(GetValue<std::string>(json, { "buildAttemptStatus" }));
}

Result<std::string> AndroidBuildApi::ProductName(const DeviceBuild& build) {
  std::string url = api_base_url_ + "/builds/" + UrlEscape(build.id) + "/" +
                    UrlEscape(build.target);
  std::vector<std::string> params;
  if (!api_key_.empty()) {
    params.push_back("key=" + UrlEscape(api_key_));
  }
  if(!project_id_.empty()){
    params.push_back("$userProject=" + UrlEscape(project_id_));
  }
  if (!params.empty()) {
    url += "?" + android::base::Join(params, "&");
  }
  auto response =
      CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess(),
            "Error fetching the product name of \""
                << build << "\". The server response was \"" << json
                << "\", and code was " << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  return CF_EXPECT(GetValue<std::string>(json, { "target", "product" }));
}

Result<std::unordered_set<std::string>> AndroidBuildApi::Artifacts(
    const DeviceBuild& build,
    const std::vector<std::string>& artifact_filenames) {
  std::string page_token = "";
  std::unordered_set<std::string> artifacts;
  do {
    std::string url = api_base_url_ + "/builds/" + UrlEscape(build.id) + "/" +
                      UrlEscape(build.target) +
                      "/attempts/latest/artifacts?maxResults=100";
    if (!artifact_filenames.empty()) {
      url += "&nameRegexp=" + UrlEscape(BuildNameRegexp(artifact_filenames));
    }
    if (!page_token.empty()) {
      url += "&pageToken=" + UrlEscape(page_token);
    }
    if (!api_key_.empty()) {
      url += "&key=" + UrlEscape(api_key_);
    }
    if(!project_id_.empty()){
      url += "&$userProject=" + UrlEscape(project_id_);
    }
    auto response =
        CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));
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
      artifacts.emplace(
          CF_EXPECT(GetValue<std::string>(artifact_json, {"name"})));
    }
  } while (!page_token.empty());
  return artifacts;
}

Result<std::unordered_set<std::string>> AndroidBuildApi::Artifacts(
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

Result<std::unordered_set<std::string>> AndroidBuildApi::Artifacts(
    const Build& build, const std::vector<std::string>& artifact_filenames) {
  auto res =
      std::visit([this, &artifact_filenames](
                     auto&& arg) { return Artifacts(arg, artifact_filenames); },
                 build);
  return CF_EXPECT(std::move(res));
}

Result<std::string> AndroidBuildApi::GetArtifactDownloadUrl(
    const DeviceBuild& build, const std::string& artifact) {
  std::string download_url_endpoint =
      api_base_url_ + "/builds/" + UrlEscape(build.id) + "/" +
      UrlEscape(build.target) + "/attempts/latest/artifacts/" +
      UrlEscape(artifact) + "/url";
  std::vector<std::string> params;
  if (!api_key_.empty()) {
    params.push_back("key=" + UrlEscape(api_key_));
  }
  if(!project_id_.empty()){
    params.push_back("$userProject=" + UrlEscape(project_id_));
  }
  if (!params.empty()) {
    download_url_endpoint += "?" + android::base::Join(params, "&");
  }
  auto response = CF_EXPECT(
      HttpGetToJson(http_client, download_url_endpoint, CF_EXPECT(Headers())));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess() || response.HttpRedirect(),
            "Error fetching the url of \"" << artifact << "\" for \"" << build
                                           << "\". The server response was \""
                                           << json << "\", and code was "
                                           << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. "
                << "Received \"" << json << "\"");
  return CF_EXPECT(GetValue<std::string>(json, { "signedUrl" }));
}

Result<void> AndroidBuildApi::ArtifactToFile(const DeviceBuild& build,
                                             const std::string& artifact,
                                             const std::string& path) {
  const auto url = CF_EXPECT(GetArtifactDownloadUrl(build, artifact));
  auto response = CF_EXPECT(HttpGetToFile(http_client, url, path));
  CF_EXPECTF(response.HttpSuccess(), "Failed to download file: {}",
             response.StatusDescription());
  return {};
}

Result<void> AndroidBuildApi::ArtifactToFile(const DirectoryBuild& build,
                                             const std::string& artifact,
                                             const std::string& path) {
  for (const auto& path : build.paths) {
    auto source = path + "/" + artifact;
    if (!FileExists(source)) {
      continue;
    }
    unlink(path.c_str());
    CF_EXPECT(symlink(source.c_str(), path.c_str()) == 0,
              "Could not create symlink from " << source << " to " << path
                                               << ": " << strerror(errno));
    return {};
  }
  return CF_ERR("Could not find artifact \"" << artifact << "\" in build \""
                                             << build << "\"");
}

Result<void> AndroidBuildApi::ArtifactToFile(const Build& build,
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

Result<std::string> AndroidBuildApi::DownloadTargetFileFromCas(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  CF_EXPECT(cas_downloader_ != nullptr, "CAS downloading is not enabled.");
  CF_EXPECT(std::holds_alternative<DeviceBuild>(build),
            "CAS downloading is only supported for DeviceBuild.");
  std::tuple<std::string, std::string> id_target = GetBuildIdAndTarget(build);
  std::string build_id = std::get<0>(id_target);
  std::string build_target = std::get<1>(id_target);
  LOG(INFO) << "Download from CAS: '" << artifact_name << "'";
  std::string target_filepath =
      ConstructTargetFilepath(target_directory, artifact_name);
  DigestsFetcher digests_fetcher =
      [&build, &target_directory,
       this](std::string filename) -> Result<std::string> {
    CF_EXPECTF(DownloadFile(build, target_directory, filename),
               "Failed to download '{}' from AB.", filename);
    return ConstructTargetFilepath(target_directory, filename);
  };
  CF_EXPECT(cas_downloader_->DownloadFile(build_id, build_target, artifact_name,
                                          target_directory, digests_fetcher));

  return {target_filepath};
}

Result<std::string> AndroidBuildApi::DownloadTargetFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::string target_filepath =
      ConstructTargetFilepath(target_directory, artifact_name);
  if (cas_downloader_ != nullptr &&
      std::holds_alternative<DeviceBuild>(build) &&
      artifact_name.find("-img-") != std::string::npos) {
    Result<std::string> result =
        DownloadTargetFileFromCas(build, target_directory, artifact_name);
    if (result.ok()) {
      return {target_filepath};
    }
    // Fallback to download from AB.
  }
  CF_EXPECT(ArtifactToFile(build, artifact_name, target_filepath),
            "Unable to download " << build << ":" << artifact_name << " to "
                                  << target_filepath);
  return {target_filepath};
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

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

#include "build_api.h"

#include <dirent.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/web/credential_source.h"

namespace cuttlefish {
namespace {

const std::string BUILD_API =
    "https://www.googleapis.com/android/internal/build/v3";

bool StatusIsTerminal(const std::string& status) {
  const static std::set<std::string> terminal_statuses = {
      "abandoned", "complete", "error", "ABANDONED", "COMPLETE", "ERROR",
  };
  return terminal_statuses.count(status) > 0;
}

bool ArtifactsContain(const std::vector<Artifact>& artifacts,
                      const std::string& name) {
  for (const auto& artifact : artifacts) {
    if (artifact.Name() == name) {
      return true;
    }
  }
  return false;
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

}  // namespace

Artifact::Artifact(const Json::Value& json_artifact) {
  name_ = json_artifact["name"].asString();
  size_ = std::stol(json_artifact["size"].asString());
  last_modified_time_ = std::stol(json_artifact["lastModifiedTime"].asString());
  md5_ = json_artifact["md5"].asString();
  content_type_ = json_artifact["contentType"].asString();
  revision_ = json_artifact["revision"].asString();
  creation_time_ = std::stol(json_artifact["creationTime"].asString());
  crc32_ = json_artifact["crc32"].asUInt();
}

std::ostream& operator<<(std::ostream& out, const DeviceBuild& build) {
  return out << "(id=\"" << build.id << "\", target=\"" << build.target
             << "\")";
}

std::ostream& operator<<(std::ostream& out, const DirectoryBuild& build) {
  auto paths = android::base::Join(build.paths, ":");
  return out << "(paths=\"" << paths << "\", target=\"" << build.target
             << "\")";
}

std::ostream& operator<<(std::ostream& out, const Build& build) {
  std::visit([&out](auto&& arg) { out << arg; }, build);
  return out;
}

DirectoryBuild::DirectoryBuild(std::vector<std::string> paths,
                               std::string target)
    : paths(std::move(paths)), target(std::move(target)), id("eng") {
  product = StringFromEnv("TARGET_PRODUCT", "");
}

BuildApi::BuildApi() : BuildApi(std::move(HttpClient::CurlClient()), nullptr) {}

BuildApi::BuildApi(std::unique_ptr<HttpClient> http_client,
                   std::unique_ptr<CredentialSource> credential_source)
    : BuildApi(std::move(http_client), nullptr, std::move(credential_source),
               "", std::chrono::seconds(0)) {}

BuildApi::BuildApi(std::unique_ptr<HttpClient> http_client,
                   std::unique_ptr<HttpClient> inner_http_client,
                   std::unique_ptr<CredentialSource> credential_source,
                   std::string api_key, const std::chrono::seconds retry_period)
    : http_client(std::move(http_client)),
      inner_http_client(std::move(inner_http_client)),
      credential_source(std::move(credential_source)),
      api_key_(std::move(api_key)),
      retry_period_(retry_period) {}

Result<std::vector<std::string>> BuildApi::Headers() {
  std::vector<std::string> headers;
  if (credential_source) {
    headers.push_back("Authorization: Bearer " +
                      CF_EXPECT(credential_source->Credential()));
  }
  return headers;
}

Result<std::string> BuildApi::LatestBuildId(const std::string& branch,
                                            const std::string& target) {
  std::string url =
      BUILD_API + "/builds?branch=" + http_client->UrlEscape(branch) +
      "&buildAttemptStatus=complete" +
      "&buildType=submitted&maxResults=1&successful=true&target=" +
      http_client->UrlEscape(target);
  if (!api_key_.empty()) {
    url += "&key=" + http_client->UrlEscape(api_key_);
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

  if (!json.isMember("builds") || json["builds"].size() != 1) {
    LOG(WARNING) << "expected to receive 1 build for \"" << target << "\" on \""
                 << branch << "\", but received " << json["builds"].size()
                 << ". Full response was " << json;
    // TODO(schuffelen): Return a failed Result here, and update ArgumentToBuild
    return "";
  }
  return json["builds"][0]["buildId"].asString();
}

Result<std::string> BuildApi::BuildStatus(const DeviceBuild& build) {
  std::string url = BUILD_API + "/builds/" + http_client->UrlEscape(build.id) +
                    "/" + http_client->UrlEscape(build.target);
  if (!api_key_.empty()) {
    url += "?key=" + http_client->UrlEscape(api_key_);
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
  std::string url = BUILD_API + "/builds/" + http_client->UrlEscape(build.id) +
                    "/" + http_client->UrlEscape(build.target);
  if (!api_key_.empty()) {
    url += "?key=" + http_client->UrlEscape(api_key_);
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

Result<std::vector<Artifact>> BuildApi::Artifacts(
    const DeviceBuild& build,
    const std::vector<std::string>& artifact_filenames) {
  std::string page_token = "";
  std::vector<Artifact> artifacts;
  do {
    std::string url = BUILD_API + "/builds/" +
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
      artifacts.emplace_back(artifact_json);
    }
  } while (page_token != "");
  return artifacts;
}

struct CloseDir {
  void operator()(DIR* dir) { closedir(dir); }
};

Result<std::vector<Artifact>> BuildApi::Artifacts(
    const DirectoryBuild& build, const std::vector<std::string>&) {
  std::vector<Artifact> artifacts;
  for (const auto& path : build.paths) {
    auto dir = std::unique_ptr<DIR, CloseDir>(opendir(path.c_str()));
    CF_EXPECT(dir != nullptr, "Could not read files from \"" << path << "\"");
    for (auto entity = readdir(dir.get()); entity != nullptr;
         entity = readdir(dir.get())) {
      artifacts.emplace_back(std::string(entity->d_name));
    }
  }
  return artifacts;
}

Result<void> BuildApi::ArtifactToCallback(const DeviceBuild& build,
                                          const std::string& artifact,
                                          HttpClient::DataCallback callback) {
  std::string download_url_endpoint =
      BUILD_API + "/builds/" + http_client->UrlEscape(build.id) + "/" +
      http_client->UrlEscape(build.target) + "/attempts/latest/artifacts/" +
      http_client->UrlEscape(artifact) + "/url";
  if (!api_key_.empty()) {
    download_url_endpoint += "?key=" + http_client->UrlEscape(api_key_);
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
  std::string url = json["signedUrl"].asString();
  auto callback_response =
      CF_EXPECT(http_client->DownloadToCallback(callback, url));
  CF_EXPECT(IsHttpSuccess(callback_response.http_code));
  return {};
}

Result<void> BuildApi::ArtifactToFile(const DeviceBuild& build,
                                      const std::string& artifact,
                                      const std::string& path) {
  std::string download_url_endpoint =
      BUILD_API + "/builds/" + http_client->UrlEscape(build.id) + "/" +
      http_client->UrlEscape(build.target) + "/attempts/latest/artifacts/" +
      http_client->UrlEscape(artifact) + "/url";
  if (!api_key_.empty()) {
    download_url_endpoint += "?key=" + http_client->UrlEscape(api_key_);
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
  std::string url = json["signedUrl"].asString();
  CF_EXPECT(CF_EXPECT(http_client->DownloadToFile(url, path)).HttpSuccess());
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

Result<Build> BuildApi::ArgumentToBuild(
    const std::string& arg, const std::string& default_build_target) {
  if (arg.find(':') != std::string::npos) {
    std::vector<std::string> dirs = android::base::Split(arg, ":");
    std::string id = dirs.back();
    dirs.pop_back();
    return DirectoryBuild(dirs, id);
  }
  size_t slash_pos = arg.find('/');
  if (slash_pos != std::string::npos &&
      arg.find('/', slash_pos + 1) != std::string::npos) {
    return CF_ERR("Build argument cannot have more than one '/' slash. Was at "
                  << slash_pos << " and " << arg.find('/', slash_pos + 1));
  }
  std::string build_target = slash_pos == std::string::npos
                                 ? default_build_target
                                 : arg.substr(slash_pos + 1);
  std::string branch_or_id =
      slash_pos == std::string::npos ? arg : arg.substr(0, slash_pos);
  std::string branch_latest_build_id =
      CF_EXPECT(LatestBuildId(branch_or_id, build_target));
  std::string build_id = branch_or_id;
  if (branch_latest_build_id != "") {
    LOG(INFO) << "The latest good build on branch \"" << branch_or_id
              << "\"with build target \"" << build_target << "\" is \""
              << branch_latest_build_id << "\"";
    build_id = branch_latest_build_id;
  }
  DeviceBuild proposed_build = DeviceBuild(build_id, build_target);
  std::string status = CF_EXPECT(BuildStatus(proposed_build));
  CF_EXPECT(status != "",
            proposed_build << " is not a valid branch or build id.");
  LOG(INFO) << "Status for build " << proposed_build << " is " << status;
  while (retry_period_ != std::chrono::seconds::zero() &&
         !StatusIsTerminal(status)) {
    LOG(INFO) << "Status is \"" << status << "\". Waiting for "
              << retry_period_.count() << " seconds.";
    std::this_thread::sleep_for(retry_period_);
    status = CF_EXPECT(BuildStatus(proposed_build));
  }
  LOG(INFO) << "Status for build " << proposed_build << " is " << status;
  proposed_build.product = CF_EXPECT(ProductName(proposed_build));
  return proposed_build;
}

Result<std::string> BuildApi::DownloadFile(const Build& build,
                                           const std::string& target_directory,
                                           const std::string& artifact_name) {
  std::vector<Artifact> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name}));
  CF_EXPECT(ArtifactsContain(artifacts, artifact_name),
            "Target " << build << " did not contain " << artifact_name);
  return DownloadTargetFile(build, target_directory, artifact_name);
}

Result<std::string> BuildApi::DownloadFileWithBackup(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name, const std::string& backup_artifact_name) {
  std::vector<Artifact> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name, backup_artifact_name}));
  std::string selected_artifact = artifact_name;
  if (!ArtifactsContain(artifacts, artifact_name)) {
    selected_artifact = backup_artifact_name;
  }
  return DownloadTargetFile(build, target_directory, selected_artifact);
}

Result<std::string> BuildApi::DownloadTargetFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::string target_filepath = target_directory + "/" + artifact_name;
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
std::string GetBuildZipName(const Build& build, const std::string& name) {
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

}  // namespace cuttlefish

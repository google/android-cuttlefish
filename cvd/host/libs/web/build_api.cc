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

#include <chrono>
#include <set>
#include <string>
#include <thread>

#include <glog/logging.h>

namespace {

const std::string BUILD_API =
    "https://www.googleapis.com/android/internal/build/v3";

bool StatusIsTerminal(const std::string& status) {
  const static std::set<std::string> terminal_statuses = {
    "abandoned",
    "complete",
    "error",
    "ABANDONED",
    "COMPLETE",
    "ERROR",
  };
  return terminal_statuses.count(status) > 0;
}

} // namespace

Artifact::Artifact(const Json::Value& json_artifact) {
  name = json_artifact["name"].asString();
  size = std::stol(json_artifact["size"].asString());
  last_modified_time = std::stol(json_artifact["lastModifiedTime"].asString());
  md5 = json_artifact["md5"].asString();
  content_type = json_artifact["contentType"].asString();
  revision = json_artifact["revision"].asString();
  creation_time = std::stol(json_artifact["creationTime"].asString());
  crc32 = json_artifact["crc32"].asUInt();
}

std::ostream& operator<<(std::ostream& out, const DeviceBuild& build) {
  return out << "(id=\"" << build.id << "\", target=\"" << build.target << "\")";
}

BuildApi::BuildApi(std::unique_ptr<CredentialSource> credential_source)
    : credential_source(std::move(credential_source)) {}

std::vector<std::string> BuildApi::Headers() {
  std::vector<std::string> headers;
  if (credential_source) {
    headers.push_back("Authorization:Bearer " + credential_source->Credential());
  }
  return headers;
}

std::string BuildApi::LatestBuildId(const std::string& branch,
                                    const std::string& target) {
  std::string url = BUILD_API + "/builds?branch=" + branch
      + "&buildType=submitted&maxResults=1&successful=true&target=" + target;
  auto response = curl.DownloadToJson(url, Headers());
  CHECK(!response.isMember("error")) << "Error fetching the latest build of \""
      << target << "\" on \"" << branch << "\". Response was " << response;

  if (!response.isMember("builds") || response["builds"].size() != 1) {
    LOG(WARNING) << "expected to receive 1 build for \"" << target << "\" on \""
        << branch << "\", but received " << response["builds"].size()
        << ". Full response was " << response;
    return "";
  }
  return response["builds"][0]["buildId"].asString();
}

std::string BuildApi::BuildStatus(const DeviceBuild& build) {
  std::string url = BUILD_API + "/builds/" + build.id + "/" + build.target;
  auto response_json = curl.DownloadToJson(url, Headers());
  CHECK(!response_json.isMember("error")) << "Error fetching the status of "
      << "build " << build << ". Response was " << response_json;

  return response_json["buildAttemptStatus"].asString();
}

std::vector<Artifact> BuildApi::Artifacts(const DeviceBuild& build) {
  std::string url = BUILD_API + "/builds/" + build.id + "/" + build.target
      + "/attempts/latest/artifacts?maxResults=1000";
  auto artifacts_json = curl.DownloadToJson(url, Headers());
  CHECK(!artifacts_json.isMember("error")) << "Error fetching the artifacts of "
      << build << ". Response was " << artifacts_json;

  std::vector<Artifact> artifacts;
  for (const auto& artifact_json : artifacts_json["artifacts"]) {
    artifacts.emplace_back(artifact_json);
  }
  return artifacts;
}

bool BuildApi::ArtifactToFile(const DeviceBuild& build,
                              const std::string& artifact,
                              const std::string& path) {
  std::string url = BUILD_API + "/builds/" + build.id + "/" + build.target
      + "/attempts/latest/artifacts/" + artifact + "?alt=media";
  return curl.DownloadToFile(url, path, Headers());
}

DeviceBuild ArgumentToBuild(BuildApi* build_api, const std::string& arg,
                            const std::string& default_build_target,
                            const std::chrono::seconds& retry_period) {
  size_t slash_pos = arg.find('/');
  if (slash_pos != std::string::npos
        && arg.find('/', slash_pos + 1) != std::string::npos) {
    LOG(FATAL) << "Build argument cannot have more than one '/' slash. Was at "
        << slash_pos << " and " << arg.find('/', slash_pos + 1);
  }
  std::string build_target = slash_pos == std::string::npos
      ? default_build_target : arg.substr(slash_pos + 1);
  std::string branch_or_id = slash_pos == std::string::npos
      ? arg: arg.substr(0, slash_pos);
  std::string branch_latest_build_id =
      build_api->LatestBuildId(branch_or_id, build_target);
  std::string build_id = branch_or_id;
  if (branch_latest_build_id != "") {
    LOG(INFO) << "The latest good build on branch \"" << branch_or_id
        << "\"with build target \"" << build_target
        << "\" is \"" << branch_latest_build_id << "\"";
    build_id = branch_latest_build_id;
  }
  DeviceBuild proposed_build = DeviceBuild(build_id, build_target);
  std::string status = build_api->BuildStatus(proposed_build);
  if (status == "") {
    LOG(FATAL) << proposed_build << " is not a valid branch or build id.";
  }
  LOG(INFO) << "Status for build " << proposed_build << " is " << status;
  while (retry_period != std::chrono::seconds::zero() && !StatusIsTerminal(status)) {
    LOG(INFO) << "Status is \"" << status << "\". Waiting for " << retry_period.count()
        << " seconds.";
    std::this_thread::sleep_for(retry_period);
    status = build_api->BuildStatus(proposed_build);
  }
  LOG(INFO) << "Status for build " << proposed_build << " is " << status;
  return proposed_build;
}

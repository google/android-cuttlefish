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

#include <string>

#include <glog/logging.h>

namespace {

const std::string BUILD_API =
    "https://www.googleapis.com/android/internal/build/v3";

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
  if (response["builds"].size() != 1) {
    LOG(WARNING) << "invalid number of builds\n";
    return "";
  }
  return response["builds"][0]["buildId"].asString();
}

std::vector<Artifact> BuildApi::Artifacts(const DeviceBuild& build) {
  std::string url = BUILD_API + "/builds/" + build.id + "/" + build.target
      + "/attempts/latest/artifacts?maxResults=1000";
  auto artifacts_json = curl.DownloadToJson(url, Headers());
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

DeviceBuild ArgumentToBuild(BuildApi* build_api, const std::string& arg) {
  size_t slash_pos = arg.find('/');
  if (slash_pos != std::string::npos
        && arg.find('/', slash_pos + 1) != std::string::npos) {
    LOG(FATAL) << "Build argument cannot have more than one '/' slash. Was at "
        << slash_pos << " and " << arg.find('/', slash_pos + 1);
  }
  std::string build_target = slash_pos == std::string::npos
      ? "aosp_cf_x86_phone-userdebug" : arg.substr(slash_pos + 1);
  std::string branch_or_id = slash_pos == std::string::npos
      ? arg: arg.substr(0, slash_pos);
  std::string branch_latest_build_id =
      build_api->LatestBuildId(branch_or_id, build_target);
  if (branch_latest_build_id != "") {
    LOG(INFO) << "The latest good build on branch \"" << branch_or_id
        << "\"with build target \"" << build_target
        << "is \"" << branch_latest_build_id << "\"";
    return DeviceBuild(branch_latest_build_id, build_target);
  } else {
    DeviceBuild proposed_build = DeviceBuild(branch_or_id, build_target);
    if (build_api->Artifacts(proposed_build).size() == 0) {
      LOG(FATAL) << '"' << branch_or_id << "\" with build target \""
          << build_target << "\" is not a valid branch or build id.";
    }
    return proposed_build;
  }
}

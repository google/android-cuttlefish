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

std::string BuildApi::LatestBuildId(const std::string& branch,
                                    const std::string& target) {
  std::string url = BUILD_API + "/builds?branch=" + branch
      + "&buildType=submitted&maxResults=1&successful=true&target=" + target;
  auto response = curl.DownloadToJson(url);
  if (response["builds"].size() != 1) {
    LOG(ERROR) << "invalid number of builds\n";
    return "";
  }
  return response["builds"][0]["buildId"].asString();
}

std::vector<Artifact> BuildApi::Artifacts(const std::string& build_id,
                                          const std::string& target,
                                          const std::string& attempt_id) {
  std::string url = BUILD_API + "/builds/" + build_id + "/" + target
      + "/attempts/" + attempt_id + "/artifacts?maxResults=1000";
  auto artifacts_json = curl.DownloadToJson(url);
  std::vector<Artifact> artifacts;
  for (const auto& artifact_json : artifacts_json["artifacts"]) {
    artifacts.emplace_back(artifact_json);
  }
  return artifacts;
}

bool BuildApi::ArtifactToFile(const std::string& build_id,
                              const std::string& target,
                              const std::string& attempt_id,
                              const std::string& artifact,
                              const std::string& path) {
  std::string url = BUILD_API + "/builds/" + build_id + "/" + target
      + "/attempts/" + attempt_id + "/artifacts/" + artifact + "?alt=media";
  return curl.DownloadToFile(url, path);
}

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

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <variant>

#include "credential_source.h"
#include "curl_wrapper.h"

class Artifact {
  std::string name;
  size_t size;
  unsigned long last_modified_time;
  std::string md5;
  std::string content_type;
  std::string revision;
  unsigned long creation_time;
  unsigned int crc32;
public:
  Artifact(const Json::Value&);
  Artifact(const std::string& name) : name(name) {}

  const std::string& Name() const { return name; }
  size_t Size() const { return size; }
  unsigned long LastModifiedTime() const { return last_modified_time; }
  const std::string& Md5() const { return md5; }
  const std::string& ContentType() const { return content_type; }
  const std::string& Revision() const { return revision; }
  unsigned long CreationTime() const { return creation_time; }
  unsigned int Crc32() const { return crc32; }
};

struct DeviceBuild {
  DeviceBuild(const std::string& id, const std::string& target) {
    this->id = id;
    this->target = target;
  }

  std::string id;
  std::string target;
  std::string product;
};

std::ostream& operator<<(std::ostream&, const DeviceBuild&);

struct DirectoryBuild {
  // TODO(schuffelen): Support local builds other than "eng"
  DirectoryBuild(const std::vector<std::string>& paths,
                 const std::string& target);

  std::vector<std::string> paths;
  std::string target;
  std::string id;
  std::string product;
};

std::ostream& operator<<(std::ostream&, const DirectoryBuild&);

using Build = std::variant<DeviceBuild, DirectoryBuild>;

std::ostream& operator<<(std::ostream&, const Build&);

class BuildApi {
  CurlWrapper curl;
  std::unique_ptr<CredentialSource> credential_source;

  std::vector<std::string> Headers();
public:
  BuildApi(std::unique_ptr<CredentialSource> credential_source);
  ~BuildApi() = default;

  std::string LatestBuildId(const std::string& branch,
                            const std::string& target);

  std::string BuildStatus(const DeviceBuild&);

  std::string ProductName(const DeviceBuild&);

  std::vector<Artifact> Artifacts(const DeviceBuild&);

  bool ArtifactToFile(const DeviceBuild& build, const std::string& artifact,
                      const std::string& path);

  std::vector<Artifact> Artifacts(const DirectoryBuild&);

  bool ArtifactToFile(const DirectoryBuild& build, const std::string& artifact,
                      const std::string& path);

  std::vector<Artifact> Artifacts(const Build& build) {
    return std::visit([this](auto&& arg) { return Artifacts(arg); }, build);
  }

  bool ArtifactToFile(const Build& build, const std::string& artifact,
                      const std::string& path) {
    return std::visit([this, &artifact, &path](auto&& arg) {
      return ArtifactToFile(arg, artifact, path);
    }, build);
  }
};

Build ArgumentToBuild(BuildApi* api, const std::string& arg,
                      const std::string& default_build_target,
                      const std::chrono::seconds& retry_period);

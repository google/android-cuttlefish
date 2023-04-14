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
#include <utility>
#include <variant>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

class Artifact {
 public:
  Artifact(const Json::Value&);
  Artifact(std::string name) : name_(std::move(name)) {}

  const std::string& Name() const { return name_; }
  size_t Size() const { return size_; }
  unsigned long LastModifiedTime() const { return last_modified_time_; }
  const std::string& Md5() const { return md5_; }
  const std::string& ContentType() const { return content_type_; }
  const std::string& Revision() const { return revision_; }
  unsigned long CreationTime() const { return creation_time_; }
  unsigned int Crc32() const { return crc32_; }

 private:
  std::string name_;
  size_t size_;
  unsigned long last_modified_time_;
  std::string md5_;
  std::string content_type_;
  std::string revision_;
  unsigned long creation_time_;
  unsigned int crc32_;
};

struct DeviceBuild {
  DeviceBuild(std::string id, std::string target)
      : id(std::move(id)), target(std::move(target)) {}

  std::string id;
  std::string target;
  std::string product;
};

std::ostream& operator<<(std::ostream&, const DeviceBuild&);

struct DirectoryBuild {
  // TODO(schuffelen): Support local builds other than "eng"
  DirectoryBuild(std::vector<std::string> paths, std::string target);

  std::vector<std::string> paths;
  std::string target;
  std::string id;
  std::string product;
};

std::ostream& operator<<(std::ostream&, const DirectoryBuild&);

using Build = std::variant<DeviceBuild, DirectoryBuild>;

std::ostream& operator<<(std::ostream&, const Build&);

class BuildApi {
 public:
  BuildApi();
  BuildApi(std::unique_ptr<HttpClient>, std::unique_ptr<CredentialSource>);
  BuildApi(std::unique_ptr<HttpClient>, std::unique_ptr<HttpClient>,
           std::unique_ptr<CredentialSource>, std::string api_key,
           const std::chrono::seconds retry_period);
  ~BuildApi() = default;

  Result<std::string> LatestBuildId(const std::string& branch,
                                    const std::string& target);

  Result<std::vector<Artifact>> Artifacts(const DeviceBuild& build,
                                          const std::string& artifact_filename);

  Result<void> ArtifactToCallback(const DeviceBuild& build,
                                  const std::string& artifact,
                                  HttpClient::DataCallback callback);

  Result<void> ArtifactToFile(const DeviceBuild& build,
                              const std::string& artifact,
                              const std::string& path);

  Result<std::vector<Artifact>> Artifacts(const DirectoryBuild& build,
                                          const std::string& artifact_filename);

  Result<void> ArtifactToFile(const DirectoryBuild& build,
                              const std::string& artifact,
                              const std::string& path);

  Result<std::vector<Artifact>> Artifacts(
      const Build& build, const std::string& artifact_filename) {
    auto res = std::visit(
        [this, &artifact_filename](auto&& arg) {
          return Artifacts(arg, artifact_filename);
        },
        build);
    return CF_EXPECT(std::move(res));
  }

  Result<void> ArtifactToFile(const Build& build, const std::string& artifact,
                              const std::string& path) {
    auto res = std::visit(
        [this, &artifact, &path](auto&& arg) {
          return ArtifactToFile(arg, artifact, path);
        },
        build);
    CF_EXPECT(std::move(res));
    return {};
  }

  Result<Build> ArgumentToBuild(const std::string& arg,
                                const std::string& default_build_target);

 private:
  Result<std::vector<std::string>> Headers();

  Result<std::string> BuildStatus(const DeviceBuild&);

  Result<std::string> ProductName(const DeviceBuild&);

  std::unique_ptr<HttpClient> http_client;
  std::unique_ptr<HttpClient> inner_http_client;
  std::unique_ptr<CredentialSource> credential_source;
  std::string api_key_;
  std::chrono::seconds retry_period_;
};

}  // namespace cuttlefish

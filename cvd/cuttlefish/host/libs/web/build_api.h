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
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/libs/web/build_string.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

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
  BuildApi(BuildApi&&) = default;
  BuildApi(std::unique_ptr<HttpClient>, std::unique_ptr<CredentialSource>);
  BuildApi(std::unique_ptr<HttpClient>, std::unique_ptr<HttpClient>,
           std::unique_ptr<CredentialSource>, std::string api_key,
           const std::chrono::seconds retry_period);
  ~BuildApi() = default;

  Result<std::optional<std::string>> LatestBuildId(const std::string& branch,
                                                   const std::string& target);

  // download the artifact from the build and apply the callback
  Result<void> ArtifactToCallback(const DeviceBuild& build,
                                  const std::string& artifact,
                                  HttpClient::DataCallback callback);

  Result<Build> GetBuild(const DeviceBuildString& build_string,
                         const std::string& fallback_target);
  Result<Build> GetBuild(const DirectoryBuildString& build_string,
                         const std::string& fallback_target);
  Result<Build> GetBuild(const BuildString& build_string,
                         const std::string& fallback_target) {
    auto result =
        std::visit([this, &fallback_target](
                       auto&& arg) { return GetBuild(arg, fallback_target); },
                   build_string);
    return CF_EXPECT(std::move(result));
  }

  Result<std::string> DownloadFile(const Build& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name);

  Result<std::string> DownloadFileWithBackup(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name,
      const std::string& backup_artifact_name);

 private:
  Result<std::vector<std::string>> Headers();

  Result<std::string> BuildStatus(const DeviceBuild&);

  Result<std::string> ProductName(const DeviceBuild&);

  Result<std::unordered_set<std::string>> Artifacts(
      const DeviceBuild& build,
      const std::vector<std::string>& artifact_filenames);

  Result<std::unordered_set<std::string>> Artifacts(
      const DirectoryBuild& build,
      const std::vector<std::string>& artifact_filenames);

  Result<std::unordered_set<std::string>> Artifacts(
      const Build& build, const std::vector<std::string>& artifact_filenames) {
    auto res = std::visit(
        [this, &artifact_filenames](auto&& arg) {
          return Artifacts(arg, artifact_filenames);
        },
        build);
    return CF_EXPECT(std::move(res));
  }

  Result<void> ArtifactToFile(const DeviceBuild& build,
                              const std::string& artifact,
                              const std::string& path);

  Result<void> ArtifactToFile(const DirectoryBuild& build,
                              const std::string& artifact,
                              const std::string& path);

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

  Result<std::string> DownloadTargetFile(const Build& build,
                                         const std::string& target_directory,
                                         const std::string& artifact_name);

  std::unique_ptr<HttpClient> http_client;
  std::unique_ptr<HttpClient> inner_http_client;
  std::unique_ptr<CredentialSource> credential_source;
  std::string api_key_;
  std::chrono::seconds retry_period_;
};

std::string GetBuildZipName(const Build& build, const std::string& name);

std::tuple<std::string, std::string> GetBuildIdAndTarget(const Build& build);

}  // namespace cuttlefish

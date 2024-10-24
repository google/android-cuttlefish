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
#include "host/libs/web/android_build_string.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {

inline constexpr char kAndroidBuildServiceUrl[] =
    "https://www.googleapis.com/android/internal/build/v3";

class BuildApi : public IBuildApi {
 public:
  BuildApi() = delete;
  BuildApi(BuildApi&&) = delete;
  virtual ~BuildApi() = default;
  BuildApi(std::unique_ptr<HttpClient> http_client,
           std::unique_ptr<HttpClient> inner_http_client,
           std::unique_ptr<CredentialSource> credential_source,
           std::string api_key, const std::chrono::seconds retry_period,
           std::string api_base_url, std::string project_id);

  Result<Build> GetBuild(const BuildString& build_string,
                         const std::string& fallback_target);

  Result<std::string> DownloadFile(const Build& build,
                                           const std::string& target_directory,
                                           const std::string& artifact_name);

  Result<std::string> DownloadFileWithBackup(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name,
      const std::string& backup_artifact_name);

  Result<std::string> GetBuildZipName(const Build& build, const std::string& name);

 private:
  Result<std::vector<std::string>> Headers();

  Result<std::optional<std::string>> LatestBuildId(const std::string& branch,
                                                   const std::string& target);

  Result<std::string> BuildStatus(const DeviceBuild&);

  Result<std::string> ProductName(const DeviceBuild&);

  Result<std::unordered_set<std::string>> Artifacts(
      const DeviceBuild& build,
      const std::vector<std::string>& artifact_filenames);

  Result<std::unordered_set<std::string>> Artifacts(
      const DirectoryBuild& build,
      const std::vector<std::string>& artifact_filenames);

  Result<std::unordered_set<std::string>> Artifacts(
      const Build& build, const std::vector<std::string>& artifact_filenames);

  Result<std::string> GetArtifactDownloadUrl(const DeviceBuild& build,
                                             const std::string& artifact);
  Result<void> ArtifactToFile(const DeviceBuild& build,
                              const std::string& artifact,
                              const std::string& path);

  Result<void> ArtifactToFile(const DirectoryBuild& build,
                              const std::string& artifact,
                              const std::string& path);

  Result<void> ArtifactToFile(const Build& build, const std::string& artifact,
                              const std::string& path);

  Result<std::string> DownloadTargetFile(const Build& build,
                                         const std::string& target_directory,
                                         const std::string& artifact_name);

  Result<Build> GetBuild(const DeviceBuildString& build_string,
                         const std::string& fallback_target);
  Result<Build> GetBuild(const DirectoryBuildString& build_string,
                         const std::string& fallback_target);

  std::unique_ptr<HttpClient> http_client;
  std::unique_ptr<HttpClient> inner_http_client;
  std::unique_ptr<CredentialSource> credential_source;
  std::string api_key_;
  std::chrono::seconds retry_period_;
  std::string api_base_url_;
  std::string project_id_;
};


std::tuple<std::string, std::string> GetBuildIdAndTarget(const Build& build);

std::optional<std::string> GetFilepath(const Build& build);

std::string ConstructTargetFilepath(const std::string& directory,
                                    const std::string& filename);

}  // namespace cuttlefish

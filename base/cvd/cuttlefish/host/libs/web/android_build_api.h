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
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/android_build_url.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/web/cas/cas_downloader.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class AndroidBuildApi : public BuildApi {
 public:
  AndroidBuildApi() = delete;
  AndroidBuildApi(AndroidBuildApi&&) = delete;
  virtual ~AndroidBuildApi() = default;
  AndroidBuildApi(HttpClient& http_client, CredentialSource* credential_source,
                  AndroidBuildUrl* android_build_url,
                  std::chrono::seconds retry_period,
                  CasDownloader* cas_downloader = nullptr);

  Result<Build> GetBuild(const BuildString& build_string) override;

  Result<std::string> DownloadFile(const Build& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override;

  Result<std::string> DownloadFileWithBackup(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name,
      const std::string& backup_artifact_name) override;

  Result<SeekableZipSource> FileReader(
      const Build&, const std::string& artifact_name) override;

 private:
  struct BuildInfo {
    std::string branch;
    std::string product;
    std::string status;
    std::string target;
    bool is_signed;
  };
  Result<BuildInfo> GetBuildInfo(std::string_view build_id,
                                 std::string_view target);
  Result<void> BlockUntilTerminalStatus(std::string_view initial_status,
                                        std::string_view build_id,
                                        std::string_view target);
  Result<std::vector<std::string>> Headers();

  Result<std::optional<std::string>> LatestBuildId(const std::string& branch,
                                                   const std::string& target);

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
  Result<std::string> DownloadTargetFileFromCas(
      const Build& build, const std::string& target_directory,
      const std::string& artifact_name);

  Result<Build> GetBuild(const DeviceBuildString& build_string);
  Result<Build> GetBuild(const DirectoryBuildString& build_string);

  Result<SeekableZipSource> FileReader(const DeviceBuild&,
                                       const std::string& artifact_name);
  Result<SeekableZipSource> FileReader(const DirectoryBuild&,
                                       const std::string& artifact_name);

  HttpClient& http_client_;
  CredentialSource* credential_source_;
  AndroidBuildUrl* android_build_url_;
  std::chrono::seconds retry_period_;
  CasDownloader* cas_downloader_;
};

std::tuple<std::string, std::string> GetBuildIdAndTarget(const Build& build);

std::optional<std::string> GetFilepath(const Build& build);

std::string ConstructTargetFilepath(const std::string& directory,
                                    const std::string& filename);

}  // namespace cuttlefish

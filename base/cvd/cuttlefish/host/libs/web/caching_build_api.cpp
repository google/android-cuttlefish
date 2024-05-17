//
// Copyright (C) 2024 The Android Open Source Project
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

#include "host/libs/web/caching_build_api.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/web/android_build_api.h"
#include "host/libs/web/credential_source.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace {

constexpr bool kOverwriteExistingFile = true;

struct CachingPaths {
  std::string build_cache;
  std::string target_artifact;
  std::string cache_artifact;
  std::string target_backup_artifact;
  std::string cache_backup_artifact;
};

bool EnsureCacheDirectory(const std::string& cache_base_path) {
  Result<void> result = EnsureDirectoryExists(cache_base_path);
  if (result.ok()) {
    return true;
  }
  LOG(WARNING) << "Failed to create cache directory \"" << cache_base_path
               << "\" with error: " << result.error().FormatForEnv();
  LOG(WARNING) << "Caching disabled";
  return false;
}

CachingPaths ConstructCachePaths(const std::string& cache_base,
                                 const Build& build,
                                 const std::string& target_directory,
                                 const std::string& artifact,
                                 const std::string& backup_artifact = "") {
  const auto [id, target] = GetBuildIdAndTarget(build);
  auto result = CachingPaths{
      .build_cache = fmt::format("{}/{}/{}", cache_base, id, target),
      .target_artifact = ConstructTargetFilepath(target_directory, artifact),
  };
  result.cache_artifact = ConstructTargetFilepath(result.build_cache, artifact);
  if (!backup_artifact.empty()) {
    result.target_backup_artifact =
        ConstructTargetFilepath(target_directory, backup_artifact);
    result.cache_backup_artifact =
        ConstructTargetFilepath(result.build_cache, backup_artifact);
  }
  return result;
}

}  // namespace

CachingBuildApi::CachingBuildApi(
    std::unique_ptr<HttpClient> http_client,
    std::unique_ptr<HttpClient> inner_http_client,
    std::unique_ptr<CredentialSource> credential_source, std::string api_key,
    const std::chrono::seconds retry_period, std::string api_base_url,
    const std::string cache_base_path)
    : BuildApi(std::move(http_client), std::move(inner_http_client),
               std::move(credential_source), std::move(api_key), retry_period,
               std::move(api_base_url)),
      cache_base_path_(std::move(cache_base_path)) {};

Result<bool> CachingBuildApi::CanCache(const std::string& target_directory) {
  return CF_EXPECT(CanHardLink(target_directory, cache_base_path_));
}

Result<std::string> CachingBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  if (!CF_EXPECT(CanCache(target_directory))) {
    LOG(WARNING)
        << "Caching disabled, unable to hard link between fetch directory \""
        << target_directory << "\" and cache directory \"" << cache_base_path_
        << "\"";
    return CF_EXPECT(
        BuildApi::DownloadFile(build, target_directory, artifact_name));
  }
  const auto paths = ConstructCachePaths(cache_base_path_, build,
                                         target_directory, artifact_name);
  CF_EXPECT(EnsureDirectoryExists(paths.build_cache));
  if (!FileExists(paths.cache_artifact)) {
    CF_EXPECT(BuildApi::DownloadFile(build, paths.build_cache, artifact_name));
  }
  return CF_EXPECT(CreateHardLink(paths.cache_artifact, paths.target_artifact,
                                  kOverwriteExistingFile));
}

Result<std::string> CachingBuildApi::DownloadFileWithBackup(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name, const std::string& backup_artifact_name) {
  if (!CF_EXPECT(CanCache(target_directory))) {
    LOG(WARNING)
        << "Caching disabled, unable to hard link between fetch directory \""
        << target_directory << "\" and cache directory \"" << cache_base_path_
        << "\"";
    return CF_EXPECT(BuildApi::DownloadFileWithBackup(
        build, target_directory, artifact_name, backup_artifact_name));
  }
  const auto paths =
      ConstructCachePaths(cache_base_path_, build, target_directory,
                          artifact_name, backup_artifact_name);
  CF_EXPECT(EnsureDirectoryExists(paths.build_cache));
  if (FileExists(paths.cache_artifact)) {
    return CF_EXPECT(CreateHardLink(paths.cache_artifact, paths.target_artifact,
                                    kOverwriteExistingFile));
  }
  if (FileExists(paths.cache_backup_artifact)) {
    return CF_EXPECT(CreateHardLink(paths.cache_backup_artifact,
                                    paths.target_backup_artifact,
                                    kOverwriteExistingFile));
  }
  const auto artifact_filepath = CF_EXPECT(BuildApi::DownloadFileWithBackup(
      build, paths.build_cache, artifact_name, backup_artifact_name));
  if (android::base::EndsWith(artifact_filepath, artifact_name)) {
    return CF_EXPECT(CreateHardLink(paths.cache_artifact, paths.target_artifact,
                                    kOverwriteExistingFile));
  }
  return CF_EXPECT(CreateHardLink(paths.cache_backup_artifact,
                                  paths.target_backup_artifact));
}

std::unique_ptr<BuildApi> CreateBuildApi(
    std::unique_ptr<HttpClient> http_client,
    std::unique_ptr<HttpClient> inner_http_client,
    std::unique_ptr<CredentialSource> credential_source, std::string api_key,
    const std::chrono::seconds retry_period, std::string api_base_url,
    const bool enable_caching, const std::string cache_base_path) {
  if (enable_caching && EnsureCacheDirectory(cache_base_path)) {
    return std::make_unique<CachingBuildApi>(
        std::move(http_client), std::move(inner_http_client),
        std::move(credential_source), std::move(api_key), retry_period,
        std::move(api_base_url), std::move(cache_base_path));
  }
  return std::make_unique<BuildApi>(
      std::move(http_client), std::move(inner_http_client),
      std::move(credential_source), std::move(api_key), retry_period,
      std::move(api_base_url));
}

}  // namespace cuttlefish

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

#include "cuttlefish/host/libs/web/caching_build_api.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "android-base/file.h"
#include "fmt/core.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/link_or_copy.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/web/digest.h"
#include "cuttlefish/host/libs/zip/cached_zip_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

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

Result<CachingPaths> ConstructCachePaths(
    const std::string& cache_base, const std::string& build_key,
    const std::string& target_directory, const std::string& artifact,
    const std::string& backup_artifact = "") {
  auto result = CachingPaths{
      .build_cache = fmt::format("{}/{}", cache_base, build_key),
      .target_artifact = ConstructTargetFilepath(target_directory, artifact),
  };
  result.cache_artifact = ConstructTargetFilepath(result.build_cache, artifact);
  CF_EXPECT(EnsureDirectoryExists(result.build_cache));
  CF_EXPECT(
      EnsureDirectoryExists(android::base::Dirname(result.target_artifact)));
  if (!backup_artifact.empty()) {
    result.target_backup_artifact =
        ConstructTargetFilepath(target_directory, backup_artifact);
    result.cache_backup_artifact =
        ConstructTargetFilepath(result.build_cache, backup_artifact);
    CF_EXPECT(EnsureDirectoryExists(
        android::base::Dirname(result.target_backup_artifact)));
  }
  return result;
}

bool IsInCache(const std::string& filepath) {
  const bool exists = FileExists(filepath);
  if (exists) {
    VLOG(1) << "Found \"" << filepath << "\" in cache";
  } else {
    VLOG(1) << "\"" << filepath << "\" not in cache";
  }
  return exists;
}

void WarnUnversioned(const Build& build, const std::string& artifact) {
  // An artifact the build does not hold at all is absent, not unversioned;
  // the download that follows reports it.
  if (!BuildHasArtifact(build, artifact)) {
    return;
  }
  LOG(WARNING) << "Not caching \"" << artifact << "\" of " << FetchLabel(build)
               << ", which reports no version for it";
}

}  // namespace

CachingBuildApi::CachingBuildApi(BuildApi& build_api,
                                 std::string cache_base_path)
    : build_api_(build_api), cache_base_path_(std::move(cache_base_path)) {};

Result<Build> CachingBuildApi::GetBuild(const BuildString& build_string) {
  return CF_EXPECT(build_api_.GetBuild(build_string));
}

Result<std::string> CachingBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  const std::string build_key = BuildCacheKey(build, artifact_name);
  if (build_key.empty()) {
    WarnUnversioned(build, artifact_name);
    return CF_EXPECT(
        build_api_.DownloadFile(build, target_directory, artifact_name));
  }
  const auto paths = CF_EXPECT(ConstructCachePaths(
      cache_base_path_, build_key, target_directory, artifact_name));
  if (IsInCache(paths.cache_artifact)) {
    std::optional<std::string> sha256 = ArtifactSha256(build, artifact_name);
    if (sha256.has_value()) {
      CF_EXPECT(VerifySha256(paths.cache_artifact, *sha256, artifact_name));
    }
  } else {
    CF_EXPECT(build_api_.DownloadFile(build, paths.build_cache, artifact_name));
  }
  return CF_EXPECT(LinkOrCopy(paths.cache_artifact, paths.target_artifact,
                              kOverwriteExistingFile));
}

Result<SeekableZipSource> CachingBuildApi::FileReader(
    const Build& build, const std::string& artifact) {
  SeekableZipSource source = CF_EXPECT(build_api_.FileReader(build, artifact));
  const std::string build_key = BuildCacheKey(build, artifact);
  if (build_key.empty()) {
    WarnUnversioned(build, artifact);
    return source;
  }
  const std::string build_cache =
      fmt::format("{}/{}", cache_base_path_, build_key);
  CF_EXPECT(EnsureDirectoryExists(build_cache));
  return CF_EXPECT(CacheZipSource(
      std::move(source), ConstructTargetFilepath(build_cache, artifact)));
}

}  // namespace cuttlefish

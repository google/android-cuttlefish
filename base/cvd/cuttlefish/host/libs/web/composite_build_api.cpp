//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/composite_build_api.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/gcs_build_api.h"
#include "cuttlefish/host/libs/web/http_build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

struct GetBuildVisitor {
  AndroidBuildApi& android;
  GcsBuildApi& gcs;
  HttpBuildApi& http;

  Result<Build> operator()(const DeviceBuildString& build_string) {
    return CF_EXPECT(android.GetBuild(build_string));
  }

  Result<Build> operator()(const DirectoryBuildString& build_string) {
    return CF_EXPECT(android.GetBuild(build_string));
  }

  Result<Build> operator()(const GcsBuildString& build_string) {
    return CF_EXPECT(gcs.GetBuild(build_string));
  }

  Result<Build> operator()(const HttpBuildString& build_string) {
    return CF_EXPECT(http.GetBuild(build_string));
  }
};

struct DownloadFileVisitor {
  AndroidBuildApi& android;
  GcsBuildApi& gcs;
  HttpBuildApi& http;
  const std::string& target_directory;
  const std::string& artifact_name;

  Result<std::string> operator()(const DeviceBuild& build) {
    return CF_EXPECT(
        android.DownloadFile(build, target_directory, artifact_name));
  }

  Result<std::string> operator()(const DirectoryBuild& build) {
    return CF_EXPECT(
        android.DownloadFile(build, target_directory, artifact_name));
  }

  Result<std::string> operator()(const GcsBuild& build) {
    return CF_EXPECT(gcs.DownloadFile(build, target_directory, artifact_name));
  }

  Result<std::string> operator()(const HttpBuild& build) {
    return CF_EXPECT(http.DownloadFile(build, target_directory, artifact_name));
  }
};

struct FileReaderVisitor {
  AndroidBuildApi& android;
  GcsBuildApi& gcs;
  HttpBuildApi& http;
  const std::string& artifact_name;

  Result<SeekableZipSource> operator()(const DeviceBuild& build) {
    return CF_EXPECT(android.FileReader(build, artifact_name));
  }

  Result<SeekableZipSource> operator()(const DirectoryBuild& build) {
    return CF_EXPECT(android.FileReader(build, artifact_name));
  }

  Result<SeekableZipSource> operator()(const GcsBuild& build) {
    return CF_EXPECT(gcs.FileReader(build, artifact_name));
  }

  Result<SeekableZipSource> operator()(const HttpBuild& build) {
    return CF_EXPECT(http.FileReader(build, artifact_name));
  }
};

}  // namespace

CompositeBuildApi::CompositeBuildApi(std::unique_ptr<AndroidBuildApi> android,
                                     std::unique_ptr<GcsBuildApi> gcs,
                                     std::unique_ptr<HttpBuildApi> http)
    : android_(std::move(android)),
      gcs_(std::move(gcs)),
      http_(std::move(http)) {}

Result<Build> CompositeBuildApi::GetBuild(const BuildString& build_string) {
  GetBuildVisitor visitor{
      .android = *android_,
      .gcs = *gcs_,
      .http = *http_,
  };
  return CF_EXPECT(std::visit(visitor, build_string));
}

Result<std::string> CompositeBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  DownloadFileVisitor visitor{
      .android = *android_,
      .gcs = *gcs_,
      .http = *http_,
      .target_directory = target_directory,
      .artifact_name = artifact_name,
  };
  return CF_EXPECT(std::visit(visitor, build));
}

Result<SeekableZipSource> CompositeBuildApi::FileReader(
    const Build& build, const std::string& artifact_name) {
  FileReaderVisitor visitor{
      .android = *android_,
      .gcs = *gcs_,
      .http = *http_,
      .artifact_name = artifact_name,
  };
  return CF_EXPECT(std::visit(visitor, build));
}

}  // namespace cuttlefish

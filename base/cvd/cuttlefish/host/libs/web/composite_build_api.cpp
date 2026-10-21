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
#include "cuttlefish/host/libs/web/android_build_string/android_build_string.h"
#include "cuttlefish/host/libs/web/directory_build_api.h"
#include "cuttlefish/host/libs/web/gcs_build_api.h"
#include "cuttlefish/host/libs/web/http_build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

CompositeBuildApi::CompositeBuildApi(std::unique_ptr<AndroidBuildApi> android,
                                     std::unique_ptr<GcsBuildApi> gcs,
                                     std::unique_ptr<HttpBuildApi> http)
    : android_(std::move(android)),
      gcs_(std::move(gcs)),
      http_(std::move(http)) {}

Result<Build> CompositeBuildApi::GetBuild(const BuildString& build_string) {
  return CF_EXPECT(std::visit(
      [&, this](const auto& build_string) -> Result<Build> {
        return CF_EXPECT(GetBuild(build_string));
      },
      build_string));
}

Result<DeviceBuild> CompositeBuildApi::GetBuild(
    const DeviceBuildString& build_string) {
  return CF_EXPECT(android_->GetBuild(build_string));
}

Result<DirectoryBuild> CompositeBuildApi::GetBuild(
    const DirectoryBuildString& build_string) {
  return CF_EXPECT(directory_.GetBuild(build_string));
}

Result<GcsBuild> CompositeBuildApi::GetBuild(
    const GcsBuildString& build_string) {
  return CF_EXPECT(gcs_->GetBuild(build_string));
}

Result<HttpBuild> CompositeBuildApi::GetBuild(
    const HttpBuildString& build_string) {
  return CF_EXPECT(http_->GetBuild(build_string));
}

Result<std::string> CompositeBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  return CF_EXPECT(std::visit(
      [&, this](const auto& build) -> Result<std::string> {
        return CF_EXPECT(DownloadFile(build, target_directory, artifact_name));
      },
      build));
}

Result<std::string> CompositeBuildApi::DownloadFile(
    const DeviceBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  return CF_EXPECT(
      android_->DownloadFile(build, target_directory, artifact_name));
}

Result<std::string> CompositeBuildApi::DownloadFile(
    const DirectoryBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  return CF_EXPECT(
      directory_.DownloadFile(build, target_directory, artifact_name));
}

Result<std::string> CompositeBuildApi::DownloadFile(
    const GcsBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  return CF_EXPECT(gcs_->DownloadFile(build, target_directory, artifact_name));
}

Result<std::string> CompositeBuildApi::DownloadFile(
    const HttpBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  return CF_EXPECT(http_->DownloadFile(build, target_directory, artifact_name));
}

Result<SeekableZipSource> CompositeBuildApi::FileReader(
    const Build& build, const std::string& artifact_name) {
  return CF_EXPECT(std::visit(
      [&, this](const auto& build) -> Result<SeekableZipSource> {
        return CF_EXPECT(FileReader(build, artifact_name));
      },
      build));
}

Result<SeekableZipSource> CompositeBuildApi::FileReader(
    const DeviceBuild& build, const std::string& artifact_name) {
  return CF_EXPECT(android_->FileReader(build, artifact_name));
}

Result<SeekableZipSource> CompositeBuildApi::FileReader(
    const DirectoryBuild& build, const std::string& artifact_name) {
  return CF_EXPECT(directory_.FileReader(build, artifact_name));
}

Result<SeekableZipSource> CompositeBuildApi::FileReader(
    const GcsBuild& build, const std::string& artifact_name) {
  return CF_EXPECT(gcs_->FileReader(build, artifact_name));
}

Result<SeekableZipSource> CompositeBuildApi::FileReader(
    const HttpBuild& build, const std::string& artifact_name) {
  return CF_EXPECT(http_->FileReader(build, artifact_name));
}

}  // namespace cuttlefish

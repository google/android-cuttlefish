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

#include <string>
#include <variant>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

CompositeBuildApi::CompositeBuildApi(BuildApi& android, BuildApi& gcs,
                                     BuildApi& http)
    : android_(android), gcs_(gcs), http_(http) {}

BuildApi& CompositeBuildApi::ApiFor(const BuildString& build_string) {
  if (std::holds_alternative<GcsBuildString>(build_string)) {
    return gcs_;
  }
  if (std::holds_alternative<HttpBuildString>(build_string)) {
    return http_;
  }
  return android_;
}

BuildApi& CompositeBuildApi::ApiFor(const Build& build) {
  if (std::holds_alternative<GcsBuild>(build)) {
    return gcs_;
  }
  if (std::holds_alternative<HttpBuild>(build)) {
    return http_;
  }
  return android_;
}

Result<Build> CompositeBuildApi::GetBuild(const BuildString& build_string) {
  return CF_EXPECT(ApiFor(build_string).GetBuild(build_string));
}

Result<std::string> CompositeBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  return CF_EXPECT(
      ApiFor(build).DownloadFile(build, target_directory, artifact_name));
}

Result<SeekableZipSource> CompositeBuildApi::FileReader(
    const Build& build, const std::string& artifact_name) {
  return CF_EXPECT(ApiFor(build).FileReader(build, artifact_name));
}

}  // namespace cuttlefish

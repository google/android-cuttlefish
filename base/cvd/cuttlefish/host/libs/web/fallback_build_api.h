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

#pragma once

#include <stddef.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Serves each request from the first backend that succeeds, trying them in
// order. A backend that cannot serve a request returns an error, and the next
// backend gets the request. When every backend fails, the error of the last
// one is returned.
//
// Example, preferring CAS over a plain Android Build download:
//
//   FallbackBuildApi<DeviceBuildString, DeviceBuild> api({
//       &cas_downloader,
//       &android_build_api,
//   });
//
// The backends are not owned and must outlive this object.
template <typename StringType, typename BuildType>
class FallbackBuildApi : public BuildApi<StringType, BuildType> {
 public:
  using Backend = BuildApi<StringType, BuildType>;

  explicit FallbackBuildApi(std::vector<Backend*> backends)
      : backends_(std::move(backends)) {}

  Result<BuildType> GetBuild(const StringType& build_string) override {
    return CF_EXPECT(FirstSuccess<BuildType>(
        [&](Backend& backend) { return backend.GetBuild(build_string); }));
  }

  Result<std::string> DownloadFile(const BuildType& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override {
    return CF_EXPECT(FirstSuccess<std::string>([&](Backend& backend) {
      return backend.DownloadFile(build, target_directory, artifact_name);
    }));
  }

  Result<SeekableZipSource> FileReader(
      const BuildType& build, const std::string& artifact_name) override {
    return CF_EXPECT(FirstSuccess<SeekableZipSource>([&](Backend& backend) {
      return backend.FileReader(build, artifact_name);
    }));
  }

 private:
  template <typename T, typename Call>
  Result<T> FirstSuccess(Call&& call) {
    Result<T> result = CF_ERR("No backends to serve the request");
    for (size_t i = 0; i < backends_.size(); ++i) {
      if (i > 0) {
        VLOG(1) << "Falling back to the next backend after: " << result.error();
      }
      result = call(*backends_[i]);
      if (result.has_value()) {
        return result;
      }
    }
    return CF_EXPECT(std::move(result));
  }

  std::vector<Backend*> backends_;
};

}  // namespace cuttlefish

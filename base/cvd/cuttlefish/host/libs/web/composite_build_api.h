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

#include <string>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Sends every build to the API that owns the source it names.
class CompositeBuildApi : public BuildApi {
 public:
  CompositeBuildApi(BuildApi& android, BuildApi& gcs, BuildApi& http);

  Result<Build> GetBuild(const BuildString& build_string) override;

  Result<std::string> DownloadFile(const Build& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override;

  Result<SeekableZipSource> FileReader(
      const Build& build, const std::string& artifact_name) override;

 private:
  BuildApi& ApiFor(const BuildString& build_string);
  BuildApi& ApiFor(const Build& build);

  BuildApi& android_;
  BuildApi& gcs_;
  BuildApi& http_;
};

}  // namespace cuttlefish

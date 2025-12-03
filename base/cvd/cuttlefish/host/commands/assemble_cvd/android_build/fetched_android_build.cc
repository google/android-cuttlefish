//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/android_build/fetched_android_build.h"

#include <memory>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/combined_android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/img_zip.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/target_files.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::unique_ptr<AndroidBuild>> FetchedAndroidBuild(
    const FetcherConfig& config, FileSource source) {
  Result<std::unique_ptr<AndroidBuild>> img_zip = ImgZip(config, source);
  Result<std::unique_ptr<AndroidBuild>> target = TargetFiles(config, source);

  if (!img_zip.ok() && !target.ok()) {
    CF_EXPECT(std::move(img_zip));
    return CF_ERR("unreachable");
  }

  std::vector<std::unique_ptr<AndroidBuild>> builds;
  if (img_zip.ok()) {
    builds.emplace_back(std::move(*img_zip));
  }
  if (target.ok()) {
    builds.emplace_back(std::move(*target));
  }
  return CF_EXPECT(
      CombinedAndroidBuild("FetchedAndroidBuild", std::move(builds)));
}

}  // namespace cuttlefish

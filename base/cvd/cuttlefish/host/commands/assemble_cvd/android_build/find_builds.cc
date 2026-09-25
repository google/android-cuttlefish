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

#include "cuttlefish/host/commands/assemble_cvd/android_build/find_builds.h"

#include <stdlib.h>

#include <memory>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_builds.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/identify_build.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/fetcher_configs.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<AndroidBuilds> FindAndroidBuilds(
    const SystemImageDirFlag& system_image_dir,
    const FetcherConfigs& fetcher_configs) {
  CF_EXPECT_EQ(system_image_dir.Size(), fetcher_configs.Size());
  std::vector<std::unique_ptr<AndroidBuild>> android_builds;

  std::vector<AndroidBuildKey> keys;
  for (size_t i = 0; i < system_image_dir.Size(); i++) {
    keys.emplace_back(system_image_dir.ForIndex(i),
                      fetcher_configs.ForInstance(i),
                      FileSource::DEFAULT_BUILD);
  }

  return CF_EXPECT(AndroidBuilds::Identify(std::move(keys)));
}

}  // namespace cuttlefish

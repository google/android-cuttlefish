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

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_dist_build.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "android-base/file.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_product_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/combined_android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/img_zip.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/target_files.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::unique_ptr<AndroidBuild>> TryDirectory(
    const std::string& dist_dir, const std::string& product_dir) {
  std::vector<std::unique_ptr<AndroidBuild>> builds;

  builds.emplace_back(CF_EXPECT(AndroidProductDir(product_dir)));
  builds.emplace_back(CF_EXPECT(ImgZip(dist_dir)));
  builds.emplace_back(CF_EXPECT(TargetFiles(dist_dir)));

  return CF_EXPECT(CombinedAndroidBuild("AndroidDistBuild", std::move(builds)));
}

bool IsRoot(std::string_view dir) { return dir == "/" || dir.empty(); }

}  // namespace

Result<std::unique_ptr<AndroidBuild>> AndroidDistBuild(
    const std::string& product_dir) {
  Result<std::unique_ptr<AndroidBuild>> attempt = CF_ERR("No `dist` directory");

  for (std::string dist_parent(product_dir); !IsRoot(dist_parent);
       dist_parent = android::base::Dirname(dist_parent)) {
    std::string dist_dir = absl::StrCat(dist_parent, "/dist");
    if (!DirectoryExists(dist_dir)) {
      continue;
    }
    if (attempt = TryDirectory(dist_dir, product_dir); attempt.ok()) {
      return std::move(*attempt);
    }
  }
  return CF_EXPECT(std::move(attempt));
}

}  // namespace cuttlefish

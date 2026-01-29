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

#include "cuttlefish/host/commands/assemble_cvd/android_build/identify_build.h"

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_dist_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_product_dir.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/combined_android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/fetched_android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/physical_partitions.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/super_image.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/config/file_source.h"
#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

AndroidBuildKey::AndroidBuildKey(std::string system_image_dir,
                                 const FetcherConfig& fetcher_config,
                                 FileSource source)
    : system_image_dir(std::move(system_image_dir)),
      fetcher_config(&fetcher_config),
      source(source) {}

bool operator<(const AndroidBuildKey& left, const AndroidBuildKey& right) {
  if (left.system_image_dir != right.system_image_dir) {
    return left.system_image_dir < right.system_image_dir;
  }
  if (left.fetcher_config != right.fetcher_config) {
    return left.fetcher_config < right.fetcher_config;
  }
  return left.source < right.source;
}

Result<std::unique_ptr<AndroidBuild>> IdentifyAndroidBuild(
    const std::string& system_image_dir, const FetcherConfig& config,
    FileSource source) {
  Result<std::unique_ptr<AndroidBuild>> res;

  if (res = FetchedAndroidBuild(config, source); res.ok()) {
  } else if (res = AndroidDistBuild(system_image_dir); res.ok()) {
    // TODO: b/473624789 - what if the dist build is older than the product build
  } else if (res = CF_EXPECT(AndroidProductDir(system_image_dir)); res.ok()) {
  } else {
    return CF_EXPECT(std::move(res));
  }
  std::unique_ptr<AndroidBuild> build = CF_EXPECT(std::move(res));

  if (res = SuperImageAsBuild(*build); res.ok()) {
    std::vector<std::unique_ptr<AndroidBuild>> builds;
    builds.emplace_back(std::move(build));
    builds.emplace_back(std::move(*res));

    build = CF_EXPECT(CombinedAndroidBuild("WithSuper", std::move(builds)));
  }

  if (res = PhysicalPartitions(*build); res.ok()) {
    std::vector<std::unique_ptr<AndroidBuild>> builds;
    builds.emplace_back(std::move(build));
    builds.emplace_back(std::move(*res));

    build = CF_EXPECT(CombinedAndroidBuild("WithPhys", std::move(builds)));
  }

  return build;
}

Result<std::unique_ptr<AndroidBuild>> IdentifyAndroidBuild(
    const AndroidBuildKey& android_build_key) {
  CF_EXPECT_NE(android_build_key.fetcher_config, nullptr);
  return CF_EXPECT(IdentifyAndroidBuild(android_build_key.system_image_dir,
                                        *android_build_key.fetcher_config,
                                        android_build_key.source));
}

std::string format_as(const AndroidBuildKey& key) {
  return absl::StrCat(
      "AndroidBuildKey { .system_image_dir = ", key.system_image_dir,
      ", .fetcher_config = ", key.fetcher_config ? "(present)" : "(null)",
      ", .source = ", key.source, " }");
}

std::ostream& operator<<(std::ostream& out, const AndroidBuildKey& key) {
  return out << format_as(key);
}

PrettyStruct Pretty(const AndroidBuildKey& key, PrettyAdlPlaceholder) {
  return PrettyStruct("AndroidBuildKey")
      .Member("system_image_dir", key.system_image_dir)
      .Member("fetcher_config", key.fetcher_config ? "(present)" : "(null)")
      .Member("source", key.source);
}

}  // namespace cuttlefish

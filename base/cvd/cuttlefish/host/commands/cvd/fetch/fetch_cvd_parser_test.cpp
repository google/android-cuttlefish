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

#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

using testing::Eq;

inline constexpr char kTargetDirectory[] = "--target_directory=/tmp/fetch_test";
inline constexpr char kDefaultBuild[] =
    "--default_build=aosp-android-latest-release/"
    "aosp_cf_x86_64_only_phone-userdebug";
inline constexpr char kCasDownloaderPath[] =
    "--cas_downloader_path=/tmp/casdownloader";
inline constexpr char kCasCacheDir[] = "--cas_cache_dir=/tmp/cas_cache";
inline constexpr char kCasCacheMaxSize[] = "--cas_cache_max_size=10000000000";

TEST(FetchCvdParserTests, CreatesCasDownloaderFlags) {
  std::string target_directory = std::string(kTargetDirectory);
  std::string default_build = std::string(kDefaultBuild);
  std::string cas_downloader_path = std::string(kCasDownloaderPath);
  std::string cas_cache_dir = std::string(kCasCacheDir);
  std::string cas_cache_max_size = std::string(kCasCacheMaxSize);
  std::vector<std::string> args = {target_directory, default_build,
                                   cas_downloader_path, cas_cache_dir,
                                   cas_cache_max_size};

  Result<FetchFlags> flagsRes = FetchFlags::Parse(args);

  EXPECT_THAT(flagsRes, IsOk());
  FetchFlags flags = flagsRes.value();
  EXPECT_THAT(
      flags.build_api_flags.cas_downloader_flags.downloader_path.value(),
      Eq("/tmp/casdownloader"));
  EXPECT_THAT(flags.build_api_flags.cas_downloader_flags.cache_dir.value(),
              Eq("/tmp/cas_cache"));
  EXPECT_THAT(flags.build_api_flags.cas_downloader_flags.cache_max_size.value(),
              Eq(10000000000));
}

TEST(FetchCvdParserTests, ParsesVendorBootImageBuildFlag) {
  std::vector<std::string> args = {
      std::string(kTargetDirectory),
      std::string(kDefaultBuild),
      "--vendor_boot_image_build=git_main/"
      "cf_x86_64_auto{vendor_boot-debug.img}",
  };

  FetchFlags flags = CF_ASSERT(FetchFlags::Parse(args));
  ASSERT_EQ(flags.vector_flags.vendor_boot_image_build.size(), 1);
  ASSERT_TRUE(flags.vector_flags.vendor_boot_image_build[0].has_value());
  EXPECT_THAT(GetFilepath(*flags.vector_flags.vendor_boot_image_build[0]),
              Eq(std::optional<std::string>("vendor_boot-debug.img")));
}

TEST(FetchCvdParserTests, ParsesSystemImageBuildFlag) {
  std::vector<std::string> args = {
      std::string(kTargetDirectory),
      std::string(kDefaultBuild),
      "--system_image_build=git_main/cf_x86_64_auto{system-custom.img}",
  };

  const Result<FetchFlags> flags_res = FetchFlags::Parse(args);

  ASSERT_THAT(flags_res, IsOk());
  ASSERT_EQ(flags_res->vector_flags.system_image_build.size(), 1);
  ASSERT_TRUE(flags_res->vector_flags.system_image_build[0].has_value());
  EXPECT_THAT(GetFilepath(*flags_res->vector_flags.system_image_build[0]),
              Eq(std::optional<std::string>("system-custom.img")));
}

}  // namespace cuttlefish

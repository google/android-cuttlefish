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

#include "host/commands/cvd/fetch/fetch_cvd_parser.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"

namespace cuttlefish {

using testing::Eq;

inline constexpr char kTargetDirectory[] = "--target_directory=/tmp/fetch_test";
inline constexpr char kDefaultBuild[] =
    "--default_build=aosp-main/aosp_cf_x86_64_phone-trunk_staging-userdebug";
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
  EXPECT_THAT(flags.build_api_flags.cas_downloader_flags.downloader_path,
              Eq("/tmp/casdownloader"));
  EXPECT_THAT(flags.build_api_flags.cas_downloader_flags.cache_dir,
              Eq("/tmp/cas_cache"));
  EXPECT_THAT(flags.build_api_flags.cas_downloader_flags.cache_max_size,
              Eq(10000000000));
}

}  // namespace cuttlefish

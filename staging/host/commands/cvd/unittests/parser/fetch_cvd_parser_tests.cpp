/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/commands/cvd/parser/fetch_cvd_parser.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"
#include "host/commands/cvd/parser/cf_flags_validator.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

using ::testing::Contains;
using ::testing::Eq;
using ::testing::Not;

namespace cuttlefish {
namespace {

Json::Value GetTestJson(const char* raw_json) {
  Json::Value json_config;
  std::string json_text(raw_json);
  ParseJsonString(json_text, json_config);
  return json_config;
}

Result<std::vector<std::string>> FetchCvdParserTestHelper(
    Json::Value& root, const std::string& target_directory,
    const std::vector<std::string>& target_subdirectories) {
  CF_EXPECT(ValidateCfConfigs(root), "Loaded Json validation failed");
  return ParseFetchCvdConfigs(root, target_directory, target_subdirectories);
}

}  // namespace

TEST(FetchCvdParserTests, SingleFetch) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "@import" : "phone",
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 4
      },
      "disk" : {
        "default_build" : "@ab/git_master/cf_x86_64_phone-userdebug",
        "download_img_zip" : true,
        "otatools" : "@ab/git_master/cf_x86_64_phone-userdebug",
        "host_package" : "@ab/git_master/cf_x86_64_phone-userdebug"
      },
      "boot" : {
        "build" : "@ab/git_master/cf_x86_64_phone-userdebug",
        "kernel" : {
          "build" : "@ab/git_master/cf_x86_64_phone-userdebug"
        },
        "bootloader" : {
          "build" : "@ab/git_master/cf_x86_64_phone-userdebug"
        }
      }
    }
  ],
  "fetch":{
    "wait_retry_period" : 20,
    "keep_downloaded_archives" : false
  }
}
  )"""";
  Json::Value json_config = GetTestJson(raw_json);

  auto result_flags = FetchCvdParserTestHelper(json_config, "/target", {"0"});
  ASSERT_THAT(result_flags, IsOk());

  const auto flags = result_flags.value();
  EXPECT_THAT(flags, Contains("--wait_retry_period=20"));
  EXPECT_THAT(flags, Contains("--keep_downloaded_archives=false"));
  EXPECT_THAT(flags, Contains("--target_directory=/target"));
  EXPECT_THAT(flags, Contains("--target_subdirectory=0"));
  EXPECT_THAT(flags,
              Contains("--default_build=git_master/cf_x86_64_phone-userdebug"));
  EXPECT_THAT(flags, Contains("--download_img_zip=true"));
  EXPECT_THAT(
      flags, Contains("--otatools_build=git_master/cf_x86_64_phone-userdebug"));
  EXPECT_THAT(
      flags,
      Contains("--host_package_build=git_master/cf_x86_64_phone-userdebug"));
  EXPECT_THAT(flags,
              Contains("--boot_build=git_master/cf_x86_64_phone-userdebug"));
  EXPECT_THAT(flags,
              Contains("--kernel_build=git_master/cf_x86_64_phone-userdebug"));
  EXPECT_THAT(
      flags,
      Contains("--bootloader_build=git_master/cf_x86_64_phone-userdebug"));
}

TEST(FetchCvdParserTests, SingleFetchNoPrefix) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "@import" : "phone",
      "disk" : {
        "default_build" : "git_master/cf_x86_64_phone-userdebug",
        "otatools" : "git_master/cf_x86_64_phone-userdebug",
        "host_package" : "git_master/cf_x86_64_phone-userdebug"
      },
      "boot" : {
        "build" : "git_master/cf_x86_64_phone-userdebug",
        "kernel" : {
          "build" : "git_master/cf_x86_64_phone-userdebug"
        },
        "bootloader" : {
          "build" : "git_master/cf_x86_64_phone-userdebug"
        }
      }
    }
  ]
}
  )"""";
  Json::Value json_config = GetTestJson(raw_json);

  auto result_flags = FetchCvdParserTestHelper(json_config, "/target", {"0"});
  ASSERT_THAT(result_flags, IsOk());
}

TEST(FetchCvdParserTests, MultiFetch) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "@import" : "phone",
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 4
      },
      "disk" : {
        "default_build" : "@ab/git_master/cf_x86_64_phone-userdebug",
        "download_img_zip" : true,
        "otatools" : "@ab/git_master/cf_x86_64_phone-userdebug",
        "host_package" : "@ab/git_master/cf_x86_64_phone-userdebug"
      },
      "boot" : {
        "build" : "@ab/git_master/cf_x86_64_phone-userdebug",
        "kernel" : {
          "build" : "@ab/git_master/cf_x86_64_phone-userdebug"
        },
        "bootloader" : {
          "build" : "@ab/git_master/cf_x86_64_phone-userdebug"
        }
      }
    },
    {
      "@import" : "wearable",
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "REQUIRED",
        "cpus" : 4
      },
      "disk" : {
        "default_build" : "@ab/git_master/cf_gwear_x86-userdebug",
        "download_img_zip" : true
      }
    }
  ],
  "fetch":{
    "wait_retry_period" : 20,
    "keep_downloaded_archives" : false
  }
}
  )"""";
  Json::Value json_config = GetTestJson(raw_json);

  auto result_flags =
      FetchCvdParserTestHelper(json_config, "/target", {"0", "1"});
  ASSERT_THAT(result_flags, IsOk());

  const auto flags = result_flags.value();
  EXPECT_THAT(flags, Contains("--wait_retry_period=20"));
  EXPECT_THAT(flags, Contains("--keep_downloaded_archives=false"));
  EXPECT_THAT(flags, Contains("--target_directory=/target"));
  EXPECT_THAT(flags, Contains("--target_subdirectory=0,1"));
  EXPECT_THAT(
      flags,
      Contains("--default_build=git_master/"
               "cf_x86_64_phone-userdebug,git_master/cf_gwear_x86-userdebug"));
  EXPECT_THAT(flags, Contains("--download_img_zip=true,true"));
  EXPECT_THAT(
      flags,
      Contains("--otatools_build=git_master/cf_x86_64_phone-userdebug,"));
  EXPECT_THAT(
      flags,
      Contains("--host_package_build=git_master/cf_x86_64_phone-userdebug,"));
  EXPECT_THAT(flags,
              Contains("--boot_build=git_master/cf_x86_64_phone-userdebug,"));
  EXPECT_THAT(flags,
              Contains("--kernel_build=git_master/cf_x86_64_phone-userdebug,"));
  EXPECT_THAT(
      flags,
      Contains("--bootloader_build=git_master/cf_x86_64_phone-userdebug,"));
}

}  // namespace cuttlefish

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

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/result_matchers.h"
#include "host/commands/cvd/parser/cf_flags_validator.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

using ::testing::Eq;
using ::testing::Ne;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::StartsWith;

namespace cuttlefish {
namespace {

Json::Value GetTestJson(const char* raw_json) {
  Json::Value json_config;
  std::string json_text(raw_json);
  ParseJsonString(json_text, json_config);
  return json_config;
}

Result<FetchCvdConfig> FetchCvdParserTestHelper(Json::Value& root) {
  CF_EXPECT(ValidateCfConfigs(root), "Loaded Json validation failed");
  return ParseFetchCvdConfigs(root);
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

  auto result_config = FetchCvdParserTestHelper(json_config);

  ASSERT_THAT(result_config, IsOk())
      << "Parsing config failed:  " << result_config.error().Trace();
  const auto top_level_config = result_config.value();
  EXPECT_THAT(top_level_config.wait_retry_period, Ne(std::nullopt));
  EXPECT_THAT(top_level_config.keep_downloaded_archives, Ne(std::nullopt));

  ASSERT_THAT(top_level_config.instances, SizeIs(1));
  const auto instance_config = *top_level_config.instances.cbegin();
  EXPECT_THAT(instance_config.default_build,
              AllOf(Ne(std::nullopt), Not(Optional(StartsWith("@ab/")))));
  EXPECT_THAT(instance_config.download_img_zip, Ne(std::nullopt));
  EXPECT_THAT(instance_config.otatools_build,
              AllOf(Ne(std::nullopt), Not(Optional(StartsWith("@ab/")))));
  EXPECT_THAT(instance_config.host_package_build,
              AllOf(Ne(std::nullopt), Not(Optional(StartsWith("@ab/")))));
  EXPECT_THAT(instance_config.boot_build,
              AllOf(Ne(std::nullopt), Not(Optional(StartsWith("@ab/")))));
  EXPECT_THAT(instance_config.kernel_build,
              AllOf(Ne(std::nullopt), Not(Optional(StartsWith("@ab/")))));
  EXPECT_THAT(instance_config.bootloader_build,
              AllOf(Ne(std::nullopt), Not(Optional(StartsWith("@ab/")))));
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

  auto result_config = FetchCvdParserTestHelper(json_config);

  ASSERT_THAT(result_config, IsOk())
      << "Parsing config failed:  " << result_config.error().Trace();
  const auto top_level_config = result_config.value();

  ASSERT_THAT(top_level_config.instances, SizeIs(1));
  const auto instance_config = *top_level_config.instances.cbegin();
  EXPECT_THAT(instance_config.default_build, Eq(std::nullopt));
  EXPECT_THAT(instance_config.otatools_build, Eq(std::nullopt));
  EXPECT_THAT(instance_config.host_package_build, Eq(std::nullopt));
  EXPECT_THAT(instance_config.boot_build, Eq(std::nullopt));
  EXPECT_THAT(instance_config.kernel_build, Eq(std::nullopt));
  EXPECT_THAT(instance_config.bootloader_build, Eq(std::nullopt));
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

  auto result_config = FetchCvdParserTestHelper(json_config);

  ASSERT_THAT(result_config, IsOk())
      << "Parsing config failed:  " << result_config.error().Trace();
  const auto top_level_config = result_config.value();
  EXPECT_THAT(top_level_config.wait_retry_period, Ne(std::nullopt));
  EXPECT_THAT(top_level_config.keep_downloaded_archives, Ne(std::nullopt));

  ASSERT_THAT(top_level_config.instances, SizeIs(2));
  auto config_iterator = top_level_config.instances.cbegin();
  const auto phone_config = *config_iterator;
  EXPECT_THAT(phone_config.default_build, Ne(std::nullopt));
  EXPECT_THAT(phone_config.download_img_zip, Ne(std::nullopt));
  EXPECT_THAT(phone_config.otatools_build, Ne(std::nullopt));
  EXPECT_THAT(phone_config.host_package_build, Ne(std::nullopt));
  EXPECT_THAT(phone_config.boot_build, Ne(std::nullopt));
  EXPECT_THAT(phone_config.kernel_build, Ne(std::nullopt));
  EXPECT_THAT(phone_config.bootloader_build, Ne(std::nullopt));

  ++config_iterator;
  const auto wearable_config = *config_iterator;
  EXPECT_THAT(wearable_config.default_build, Ne(std::nullopt));
  EXPECT_THAT(wearable_config.download_img_zip, Ne(std::nullopt));
  EXPECT_THAT(wearable_config.otatools_build, Eq(std::nullopt));
  EXPECT_THAT(wearable_config.host_package_build, Eq(std::nullopt));
  EXPECT_THAT(wearable_config.boot_build, Eq(std::nullopt));
  EXPECT_THAT(wearable_config.kernel_build, Eq(std::nullopt));
  EXPECT_THAT(wearable_config.bootloader_build, Eq(std::nullopt));
}

}  // namespace cuttlefish

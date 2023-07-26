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

#include <gtest/gtest.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_flags_validator.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

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
        "default_build" : "git_master/cf_x86_64_phone-userdebug",
        "download_img_zip" : true
      }
    }
  ],
  "wait_retry_period" : 20,
  "keep_downloaded_archives" : false
}
  )"""";
  Json::Value json_config = GetTestJson(raw_json);

  auto result_config = FetchCvdParserTestHelper(json_config);

  EXPECT_TRUE(result_config.ok())
      << "Parsing config failed:  " << result_config.error().Trace();
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
        "default_build" : "git_master/cf_x86_64_phone-userdebug",
        "download_img_zip" : true
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
        "default_build" : "git_master/cf_gwear_x86-userdebug",
        "download_img_zip" : true
      }
    }
  ],
  "wait_retry_period" : 20,
  "keep_downloaded_archives" : false
}
  )"""";
  Json::Value json_config = GetTestJson(raw_json);

  auto result_config = FetchCvdParserTestHelper(json_config);

  EXPECT_TRUE(result_config.ok())
      << "Parsing config failed:  " << result_config.error().Trace();
}

}  // namespace cuttlefish

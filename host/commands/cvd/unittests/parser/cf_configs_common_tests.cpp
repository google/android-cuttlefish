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

#include "host/commands/cvd/parser/cf_configs_common.h"

#include <string>

#include <gtest/gtest.h>
#include <json/json.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

namespace cuttlefish {

TEST(CfConfigsCommonTests, GenerateGflagSingleInstance) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "@import" : "phone",
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 4
      }
    }
  ],
  "wait_retry_period" : 20,
  "keep_downloaded_archives" : false
}
  )"""";

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  ASSERT_TRUE(json_config.isMember("instances"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(0));
  ASSERT_TRUE(json_config["instances"][0].isMember("vm"));
  ASSERT_TRUE(json_config["instances"][0]["vm"].isMember("cpus"));
  auto result = GenerateGflag(json_config["instances"], "cpus", {"vm", "cpus"});

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "--cpus=4");
}

TEST(CfConfigsCommonTests, GenerateGflagMultiInstance) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "@import" : "phone",
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 4
      }
    },
    {
      "@import" : "phone",
      "vm" : {
        "memory_mb" : 4096,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 2
      }
    }
  ],
  "wait_retry_period" : 20,
  "keep_downloaded_archives" : false
}
  )"""";

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  ASSERT_TRUE(json_config.isMember("instances"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(0));
  ASSERT_TRUE(json_config["instances"][0].isMember("vm"));
  ASSERT_TRUE(json_config["instances"][0]["vm"].isMember("cpus"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(1));
  ASSERT_TRUE(json_config["instances"][1].isMember("vm"));
  ASSERT_TRUE(json_config["instances"][1]["vm"].isMember("cpus"));
  auto result = GenerateGflag(json_config["instances"], "cpus", {"vm", "cpus"});

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), "--cpus=4,2");
}

TEST(CfConfigsCommonTests, GenerateGflagMissingValue) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "@import" : "phone",
      "vm" : {
        "memory_mb" : 8192,
        "cpus" : 4
      }
    }
  ],
  "wait_retry_period" : 20,
  "keep_downloaded_archives" : false
}
  )"""";

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  ASSERT_TRUE(json_config.isMember("instances"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(0));
  ASSERT_TRUE(json_config["instances"][0].isMember("vm"));
  auto result = GenerateGflag(json_config["instances"], "setupwizard_mode",
                              {"vm", "setupwizard_mode"});

  EXPECT_FALSE(result.ok());
}

}  // namespace cuttlefish

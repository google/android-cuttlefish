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
#include "common/libs/utils/result_matchers.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

namespace cuttlefish {

TEST(CfConfigsCommonTests, ValidateConfigValidationSuccess) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 4
      }
    }
  ]
}
  )"""";

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  ASSERT_TRUE(json_config.isMember("instances"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(0));
  ASSERT_TRUE(json_config["instances"][0].isMember("vm"));

  auto success_validator = std::function<Result<void>(const std::string&)>(
      [](const std::string&) -> Result<void> { return {}; });
  auto result = ValidateConfig(json_config["instances"][0], success_validator,
                               {"vm", "cpus"});

  EXPECT_THAT(result, IsOk());
}

TEST(CfConfigsCommonTests, ValidateConfigValidationFailure) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 4
      }
    }
  ]
}
  )"""";

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  ASSERT_TRUE(json_config.isMember("instances"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(0));
  ASSERT_TRUE(json_config["instances"][0].isMember("vm"));

  auto error_validator = std::function<Result<void>(const std::string&)>(
      [](const std::string&) -> Result<void> { return CF_ERR("placeholder"); });
  auto result = ValidateConfig(json_config["instances"][0], error_validator,
                               {"vm", "cpus"});

  EXPECT_THAT(result, IsError());
}

TEST(CfConfigsCommonTests, ValidateConfigFieldDoesNotExist) {
  const char* raw_json = R""""(
{
  "instances" : [
    {
      "vm" : {
        "memory_mb" : 8192,
        "setupwizard_mode" : "OPTIONAL",
        "cpus" : 4
      }
    }
  ]
}
  )"""";

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  ASSERT_TRUE(json_config.isMember("instances"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(0));

  auto success_validator = std::function<Result<void>(const std::string&)>(
      [](const std::string&) -> Result<void> { return {}; });
  auto result = ValidateConfig(json_config["instances"][0], success_validator,
                               {"disk", "cpus"});

  EXPECT_THAT(result, IsOk());
}

TEST(CfConfigsCommonTests, InitConfigTopLevel) {
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

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  EXPECT_FALSE(json_config.isMember("api_key"));

  auto result =
      InitConfig(json_config, Json::Value::nullSingleton(), {"api_key"});

  EXPECT_THAT(result, IsOk());
  EXPECT_TRUE(json_config.isMember("api_key"));
  EXPECT_TRUE(json_config["api_key"].isNull());
}

TEST(CfConfigsCommonTests, InitConfigInstanceLevel) {
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

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  ASSERT_TRUE(json_config.isMember("instances"));
  ASSERT_TRUE(json_config["instances"].isValidIndex(0));
  ASSERT_TRUE(json_config["instances"][0].isMember("disk"));
  EXPECT_FALSE(json_config["instances"][0]["disk"].isMember(
      "download_target_files_zip"));

  auto result =
      InitConfig(json_config["instances"][0], Json::Value::nullSingleton(),
                 {"disk", "download_target_files_zip"});

  EXPECT_THAT(result, IsOk());
  EXPECT_TRUE(json_config["instances"][0]["disk"].isMember(
      "download_target_files_zip"));
  EXPECT_TRUE(json_config["instances"][0]["disk"]["download_target_files_zip"]
                  .isNull());
}

TEST(CfConfigsCommonTests, InitConfigInstanceLevelMissingLevel) {
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
  EXPECT_FALSE(json_config["instances"][0].isMember("disk"));

  auto result =
      InitConfig(json_config["instances"][0], Json::Value::nullSingleton(),
                 {"disk", "download_target_files_zip"});

  EXPECT_THAT(result, IsOk());
  ASSERT_TRUE(json_config["instances"][0].isMember("disk"));
  EXPECT_TRUE(json_config["instances"][0]["disk"].isMember(
      "download_target_files_zip"));
  EXPECT_TRUE(json_config["instances"][0]["disk"]["download_target_files_zip"]
                  .isNull());
}

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

  EXPECT_THAT(result, IsOkAndValue("--cpus=4"));
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

  EXPECT_THAT(result, IsOkAndValue("--cpus=4,2"));
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

  EXPECT_THAT(result, IsError());
}

TEST(ValidateTests, ValidateArrayTypeSuccess) {
  const char* raw_json = R""""(
  [
    "value1",
    "value2",
    "value3"
  ]
  )"""";
  const auto validation_definition =
      ConfigNode{.type = Json::ValueType::arrayValue,
                 .children = {
                     {kArrayValidationSentinel,
                      ConfigNode{.type = Json::ValueType::stringValue}},
                 }};

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  auto result = Validate(json_config, validation_definition);
  EXPECT_THAT(result, IsOk());
}

TEST(ValidateTests, ValidateArrayTypeFailure) {
  const char* raw_json = R""""(
  [
    "value1",
    "value2",
    "value3"
  ]
  )"""";
  const auto validation_definition =
      ConfigNode{.type = Json::ValueType::arrayValue,
                 .children = {
                     {"foo", ConfigNode{.type = Json::ValueType::stringValue}},
                 }};

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  auto result = Validate(json_config, validation_definition);
  EXPECT_THAT(result, IsError());
}

TEST(ValidateTests, ValidateObjectTypeSuccess) {
  const char* raw_json = R""""(
  {
    "key" : "value",
    "key2" : 1234,
    "key3" : {
      "key4" : true
    }
  }
  )"""";
  const auto validation_definition = ConfigNode{
      .type = Json::ValueType::objectValue,
      .children = {
          {"key", ConfigNode{.type = Json::ValueType::stringValue}},
          {"key2", ConfigNode{.type = Json::ValueType::uintValue}},
          {"key3",
           ConfigNode{
               .type = Json::ValueType::objectValue,
               .children =
                   {
                       {"key4",
                        ConfigNode{.type = Json::ValueType::booleanValue}},
                   }}},
      }};

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  auto result = Validate(json_config, validation_definition);
  EXPECT_THAT(result, IsOk());
}

TEST(ValidateTests, ValidateObjectTypeFailure) {
  const char* raw_json = R""""(
  {
    "key" : "value",
    "key2" : 1234,
    "key3" : {
      "key4" : true
    }
  }
  )"""";
  const auto validation_definition = ConfigNode{
      .type = Json::ValueType::objectValue,
      .children = {
          {"key", ConfigNode{.type = Json::ValueType::booleanValue}},
          {"key2", ConfigNode{.type = Json::ValueType::uintValue}},
          {"key3",
           ConfigNode{
               .type = Json::ValueType::objectValue,
               .children =
                   {
                       {"key4",
                        ConfigNode{.type = Json::ValueType::stringValue}},
                   }}},
      }};

  Json::Value json_config;
  std::string json_text(raw_json);
  ASSERT_TRUE(ParseJsonString(json_text, json_config))
      << "Invalid JSON string for test";

  auto result = Validate(json_config, validation_definition);
  EXPECT_THAT(result, IsError());
}

}  // namespace cuttlefish

/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <algorithm>
#include <fstream>
#include <iostream>

#include <android-base/file.h>
#include <gtest/gtest.h>

#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/unittests/parser/test_common.h"
namespace cuttlefish {
TEST(FlagsParserTest, ParseInvalidJson) {
  const char* test_string = R""""(
    instances=50;
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_FALSE(ParseJsonString(json_text, json_configs));
}

TEST(FlagsParserTest, ParseJsonWithSpellingError) {
  const char* test_string = R""""(
{
    "Insta" :
    [
        {
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_FALSE(serialized_data.ok());
}

TEST(FlagsParserTest, ParseBasicJsonSingleInstances) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "vm": {
            "crosvm":{
            }
          }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, "--num_instances=1"))
      << "num_instances flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseBasicJsonTwoInstances) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "vm": {
            "crosvm":{
            }
          }
        },
        {
          "vm": {
            "crosvm":{
            }
          }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, "--num_instances=2"))
      << "num_instances flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseNetSimFlagEmptyJson) {
  const char* test_string = R""""(
{
  "instances" :
  [
        {
          "vm": {
            "crosvm":{
            }
          }
        }
  ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--netsim_bt=false)"))
      << "netsim_bt flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseNetSimFlagEnabled) {
  const char* test_string = R""""(
{
   "netsim_bt": true,
     "instances" :
     [
        {
          "vm": {
            "crosvm":{
            }
          }
        }
      ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--netsim_bt=true)"))
      << "netsim_bt flag is missing or wrongly formatted";
}

}  // namespace cuttlefish

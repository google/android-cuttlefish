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

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_FALSE(ParseJsonString(strjson, json_configs));
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

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_TRUE(ParseJsonString(strjson, json_configs));
  EXPECT_FALSE(ParseCvdConfigs(json_configs, serialized_data));
}

TEST(FlagsParserTest, ParseBasicJsonSingleInstances) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
        }
    ]
}
  )"""";

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_TRUE(ParseJsonString(strjson, json_configs));
  EXPECT_TRUE(ParseCvdConfigs(json_configs, serialized_data));
  EXPECT_TRUE(FindConfig(serialized_data, "--num_instances=1"));
}

TEST(FlagsParserTest, ParseBasicJsonTwoInstances) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
        },
        {

        }
    ]
}
  )"""";

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_TRUE(ParseJsonString(strjson, json_configs));
  EXPECT_TRUE(ParseCvdConfigs(json_configs, serialized_data));
  EXPECT_TRUE(FindConfig(serialized_data, "--num_instances=2"));
}

}  // namespace cuttlefish
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

#include <string>

#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(ConfigFlagsParserTest, ParseSingleInstanceConfig) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "config": "phone"
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--config=phone"))
      << "config flag is missing or wrongly formatted";
}

TEST(ConfigFlagsParserTest, ParseMultiInstanceConfig) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "config": "phone"
        },
        {
          "config": "tv"
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--config=phone,tv"))
      << "config multi-instance flag is missing or wrongly formatted";
}

TEST(ConfigFlagsParserTest, ParseMultiInstanceConfigPartial) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "config": "phone"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--config=phone,"))
      << "config partial multi-instance flag is missing or wrongly formatted";
}

}  // namespace
}  // namespace cuttlefish

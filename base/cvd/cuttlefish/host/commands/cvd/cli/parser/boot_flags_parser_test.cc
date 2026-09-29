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
#include <sstream>
#include <string>

#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_FALSE(FindConfig(*serialized_data, R"(--netsim_bt=)"))
      << "netsim_bt flag is set";
  EXPECT_FALSE(FindConfig(*serialized_data, R"(--netsim_uwb=)"))
      << "netsim_uwb flag is set";
}

TEST(BootFlagsParserTest, ParseNetSimFlagEnabled) {
  const char* test_string = R""""(
{
   "netsim_bt": false,
   "netsim_uwb": true,
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--netsim_bt=false)"))
      << "netsim_bt flag is missing or wrongly formatted";
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--netsim_uwb=true)"))
      << "netsim_uwb flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseNetSimArgs) {
  const char* test_string = R""""(
{
   "netsim_args": ["--wifi-instance=1", "--bt-instance=2"],
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

  auto json_configs = ParseJson(test_string);
  EXPECT_THAT(json_configs, IsOk());
  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data,
                         "--netsim_args=--wifi-instance=1 --bt-instance=2"))
      << "netsim_args flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseNetSimArgsWhitespaceError) {
  const char* test_string = R""""(
{
   "netsim_args": ["--wifi-instance=1", "--bt-instance 2"],
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

  auto json_configs = ParseJson(test_string);
  EXPECT_THAT(json_configs, IsOk());
  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsError());
}

}  // namespace
}  // namespace cuttlefish

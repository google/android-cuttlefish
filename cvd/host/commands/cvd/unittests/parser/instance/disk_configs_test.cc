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

#include <gtest/gtest.h>
#include "host/commands/cvd/parser/launch_cvd_parser.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

namespace cuttlefish {

TEST(BootFlagsParserTest, ParseTwoInstancesBlankDataImageEmptyJson) {
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

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--blank_data_image_mb=unset,unset)"))
      << "blank_data_image_mb flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesBlankDataImagePartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
            }
        },
        {
            "disk": {
                "blank_data_image_mb": 2048
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--blank_data_image_mb=unset,2048)"))
      << "blank_data_image_mb flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesBlankDataImageFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "blank_data_image_mb": 2048
            }
        },
        {
            "disk": {
                "blank_data_image_mb": 4096
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--blank_data_image_mb=2048,4096)"))
      << "blank_data_image_mb flag is missing or wrongly formatted";
}

}  // namespace cuttlefish

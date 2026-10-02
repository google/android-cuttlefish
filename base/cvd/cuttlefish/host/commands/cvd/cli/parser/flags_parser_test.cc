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

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_FALSE(serialized_data.has_value());
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--num_instances=2"))
      << "num_instances flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaSplaneSingleInstance) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {}
              }
            ]
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
  EXPECT_TRUE(
      FindConfig(*serialized_data, "--media=v4l2_emulated_camera_splane"))
      << "media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaSplaneTwoDevices) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {}
              },
              {
                "v4l2_emulated_camera_splane": {}
              }
            ]
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
  EXPECT_EQ(std::count(serialized_data->begin(), serialized_data->end(),
                       "--media=v4l2_emulated_camera_splane"),
            2);
}

TEST(FlagsParserTest, ParseMediaMplane) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_mplane": {}
              }
            ]
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
  EXPECT_TRUE(
      FindConfig(*serialized_data, "--media=v4l2_emulated_camera_mplane"))
      << "media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaV4l2Proxy) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_proxy": {
                  "device_path": "/dev/video0"
                }
              }
            ]
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--media=v4l2_proxy"))
      << "media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaTwoInstancesBothWithDevices) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "BACK"
              }
            ]
          }
        },
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "FRONT"
              }
            ]
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
  EXPECT_TRUE(FindConfig(*serialized_data,
                         "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"
                         "v4l2_emulated_camera_splane:lens_facing=FRONT"))
      << "media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaTwoInstancesMultipleRepeatedDevices) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "FRONT"
              },
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "BACK"
              }
            ]
          }
        },
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "FRONT"
              },
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "BACK"
              }
            ]
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
  EXPECT_TRUE(
      FindConfig(*serialized_data,
                 "--media=v4l2_emulated_camera_splane:lens_facing=FRONT,"
                 "v4l2_emulated_camera_splane:lens_facing=FRONT"))
      << "First media flag is missing or wrongly formatted";
  EXPECT_TRUE(FindConfig(*serialized_data,
                         "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"
                         "v4l2_emulated_camera_splane:lens_facing=BACK"))
      << "Second media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaTwoInstancesOnlyFirstHasDevice) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "BACK"
              }
            ]
          }
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

  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);

  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(
      FindConfig(*serialized_data,
                 "--media=v4l2_emulated_camera_splane:lens_facing=BACK,"))
      << "media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaTwoInstancesOnlySecondHasDevice) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
        },
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {},
                "lens_facing": "FRONT"
              }
            ]
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
  EXPECT_TRUE(
      FindConfig(*serialized_data,
                 "--media=,v4l2_emulated_camera_splane:lens_facing=FRONT"))
      << "media flag is missing or wrongly formatted";
}

}  // namespace
}  // namespace cuttlefish

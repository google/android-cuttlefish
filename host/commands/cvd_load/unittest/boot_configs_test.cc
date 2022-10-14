/*
 * Copyright (C) 2015-2022 The Android Open Source Project
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
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd_load/unittest/test_common.h"

namespace cuttlefish {
TEST(BootFlagsParserTest, ParseTwoInstancesExtraBootConfigFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(serialized_data, R"(--extra_bootconfig_args="","")"));
}

TEST(BootFlagsParserTest, ParseTwoInstancesExtraBootConfigFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "boot": {
            }
        },
        {
            "boot": {
                "extra_bootconfig_args": "androidboot.X=Y"
            }
        }
    ]
}
  )"""";

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_TRUE(ParseJsonString(strjson, json_configs));
  EXPECT_TRUE(ParseCvdConfigs(json_configs, serialized_data));
  EXPECT_TRUE(FindConfig(serialized_data,
                         R"(--extra_bootconfig_args="","androidboot.X=Y")"));
}

TEST(BootFlagsParserTest, ParseTwoInstancesExtraBootConfigFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "boot": {
                "extra_bootconfig_args": "androidboot.X=Y"
            }
        },
        {
            "boot": {
                "extra_bootconfig_args": "androidboot.X=Z"
            }
        }
    ]
}
  )"""";

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_TRUE(ParseJsonString(strjson, json_configs));
  EXPECT_TRUE(ParseCvdConfigs(json_configs, serialized_data));
  EXPECT_TRUE(FindConfig(
      serialized_data,
      R"(--extra_bootconfig_args="androidboot.X=Y","androidboot.X=Z")"));
}

TEST(BootFlagsParserTest, ParseTwoInstancesSerialNumberFlagEmptyJson) {
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
  EXPECT_TRUE(
      FindConfig(serialized_data,
                 R"(--serial_number="CUTTLEFISHCVD01","CUTTLEFISHCVD01")"));
}

TEST(BootFlagsParserTest, ParseTwoInstancesSerialNumberFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "boot": {
                "security": {
                }
            }
        },
        {
            "boot": {
                "security": {
                    "serial_number": "CUTTLEFISHCVD101"
                }
            }
        }
    ]
}
  )"""";

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_TRUE(ParseJsonString(strjson, json_configs));
  EXPECT_TRUE(ParseCvdConfigs(json_configs, serialized_data));
  EXPECT_TRUE(
      FindConfig(serialized_data,
                 R"(--serial_number="CUTTLEFISHCVD01","CUTTLEFISHCVD101")"));
}

TEST(BootFlagsParserTest, ParseTwoInstancesSerialNumberFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "boot": {
                "security": {
                    "serial_number": "CUTTLEFISHCVD101"
                }
            }
        },
        {
            "boot": {
                "security": {
                    "serial_number": "CUTTLEFISHCVD102"
                }
            }
        }
    ]
}
  )"""";

  std::vector<std::string> serialized_data;
  Json::Value json_configs;
  std::string strjson(test_string);

  EXPECT_TRUE(ParseJsonString(strjson, json_configs));
  EXPECT_TRUE(ParseCvdConfigs(json_configs, serialized_data));
  EXPECT_TRUE(
      FindConfig(serialized_data,
                 R"(--serial_number="CUTTLEFISHCVD101","CUTTLEFISHCVD102")"));
}

}  // namespace cuttlefish
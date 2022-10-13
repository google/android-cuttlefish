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
#include "host/commands/cvd_load/unittest/test_common.h"

namespace cuttlefish {

TEST(VmFlagsParserTest, ParseTwoInstancesCpuFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(serialized_data, "--cpus=2,2"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesCpuFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
            }
        },
        {
            "vm": {
                "cpus": 4
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
  EXPECT_TRUE(FindConfig(serialized_data, "--cpus=2,4"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesCpuFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "cpus": 4
            }
        },
        {
            "vm": {
                "cpus": 6
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
  EXPECT_TRUE(FindConfig(serialized_data, "--cpus=4,6"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesMemoryFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(serialized_data, "--memory_mb=0,0"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesMemoryFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
            }
        },
        {
            "vm": {
                "memory_mb": 4069
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
  EXPECT_TRUE(FindConfig(serialized_data, "--memory_mb=0,4069"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesMemoryFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "memory_mb": 4069
            }
        },
        {
            "vm": {
                "memory_mb": 8192
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
  EXPECT_TRUE(FindConfig(serialized_data, "--memory_mb=4069,8192"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesVmManagerFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(serialized_data, R"(--vm_manager="","")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesVmManagerFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
            }
        },
        {
            "vm": {
                "vm_manager": "crosvm"
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
  EXPECT_TRUE(FindConfig(serialized_data, R"(--vm_manager="","crosvm")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesVmManagerFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "vm_manager": "qemu_cli"
            }
        },
        {
            "vm": {
                "vm_manager": "crosvm"
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
      FindConfig(serialized_data, R"(--vm_manager="qemu_cli","crosvm")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesSetupWizardFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(serialized_data,
                         R"(--setupwizard_mode="DISABLED","DISABLED")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesSetupWizardFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
            }
        },
        {
            "vm": {
                "setupwizard_mode": "ENABLED"
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
                         R"(--setupwizard_mode="DISABLED","ENABLED")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesSetupWizardFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "setupwizard_mode": "ENABLED"
            }
        },
        {
            "vm": {
                "setupwizard_mode": "ENABLED"
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
      FindConfig(serialized_data, R"(--setupwizard_mode="ENABLED","ENABLED")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesUuidFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(
      serialized_data,
      R"(--uuid="699acfc4-c8c4-11e7-882b-5065f31dc101","699acfc4-c8c4-11e7-882b-5065f31dc101")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesUuidFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
            }
        },
        {
            "vm": {
                "uuid": "870acfc4-c8c4-11e7-99ac-5065f31dc250"
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
      R"(--uuid="699acfc4-c8c4-11e7-882b-5065f31dc101","870acfc4-c8c4-11e7-99ac-5065f31dc250")"));
}

TEST(VmFlagsParserTest, ParseTwoInstancesUuidFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "uuid": "870acfc4-c8c4-11e7-99ac-5065f31dc250"
            }
        },
        {
            "vm": {
                "uuid": "870acfc4-c8c4-11e7-99ac-5065f31dc251"
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
      R"(--uuid="870acfc4-c8c4-11e7-99ac-5065f31dc250","870acfc4-c8c4-11e7-99ac-5065f31dc251")"));
}

}  // namespace cuttlefish
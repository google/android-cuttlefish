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
#include "host/commands/cvd/unittests/parser/test_common.h"

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

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--extra_bootconfig_args=,)"))
      << "extra_bootconfig_args flag is missing or wrongly formatted";
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

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data,
                         R"(--extra_bootconfig_args=,androidboot.X=Y)"))
      << "extra_bootconfig_args flag is missing or wrongly formatted";
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

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data,
                 R"(--extra_bootconfig_args=androidboot.X=Y,androidboot.X=Z)"))
      << "extra_bootconfig_args flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesBootAnimationFlagEmptyJson) {
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
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--enable_bootanimation=true,true)"))
      << "enable_bootanimation flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesBootAnimationFlagPartialJson) {
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
                "enable_bootanimation": false
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
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--enable_bootanimation=true,false)"))
      << "enable_bootanimation flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesBootAnimationFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "boot": {
                "enable_bootanimation": false
            }
        },
        {
            "boot": {
                "enable_bootanimation": false
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
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--enable_bootanimation=false,false)"))
      << "enable_bootanimation flag is missing or wrongly formatted";
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

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data,
                         R"(--serial_number=CUTTLEFISHCVD01,CUTTLEFISHCVD01)"))
      << "serial_number flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesSerialNumberFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "security": {
            }
        },
        {
            "security": {
                "serial_number": "CUTTLEFISHCVD101"
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
  EXPECT_TRUE(FindConfig(*serialized_data,
                         R"(--serial_number=CUTTLEFISHCVD01,CUTTLEFISHCVD101)"))
      << "serial_number flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesSerialNumberFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "security": {
                "serial_number": "CUTTLEFISHCVD101"
            }
        },
        {
            "security": {
                "serial_number": "CUTTLEFISHCVD102"
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
  EXPECT_TRUE(FindConfig(
      *serialized_data, R"(--serial_number=CUTTLEFISHCVD101,CUTTLEFISHCVD102)"))
      << "serial_number flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesKernelCmdFlagEmptyJson) {
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
  auto serialized_data = ParseCvdConfigs(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--extra_kernel_cmdline=,)"))
      << "extra_kernel_cmdline flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesKernelCmdFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "boot": {
                "kernel": {
                }
            }
        },
        {
            "boot": {
                "kernel": {
                    "extra_kernel_cmdline": "androidboot.selinux=permissive"
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
  EXPECT_TRUE(
      FindConfig(*serialized_data,
                 R"(--extra_kernel_cmdline=,androidboot.selinux=permissive)"))
      << "extra_kernel_cmdline flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesKernelCmdFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "boot": {
                "kernel": {
                    "extra_kernel_cmdline": "androidboot.selinux=permissive"
                }
            }
        },
        {
            "boot": {
                "kernel": {
                    "extra_kernel_cmdline": "lpm_levels.sleep_disabled=1"
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
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--extra_kernel_cmdline=androidboot.selinux=permissive,lpm_levels.sleep_disabled=1)"))
      << "extra_kernel_cmdline flag is missing or wrongly formatted";
}

}  // namespace cuttlefish

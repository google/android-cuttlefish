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

#include "host/commands/cvd/parser/launch_cvd_parser.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

namespace cuttlefish {

TEST(VmFlagsParserTest, ParseTwoInstancesCpuFlagEmptyJson) {
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, "--cpus=2,2"))
      << "cpus flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesCpuFlagPartialJson) {
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
                },
                "cpus": 4
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--cpus=2,4"))
      << "cpus flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesCpuFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                },
                "cpus": 4
            }
        },
        {
            "vm": {
                "crosvm":{
                },
                "cpus": 6
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--cpus=4,6"))
      << "cpus flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesMemoryFlagEmptyJson) {
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, "--memory_mb=2048,2048"))
      << "memory_mb flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesMemoryFlagPartialJson) {
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
                },
                "memory_mb": 4069
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--memory_mb=2048,4069"))
      << "memory_mb flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesMemoryFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                },
                "memory_mb": 4069
            }
        },
        {
            "vm": {
                "crosvm":{
                },
                "memory_mb": 8192
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--memory_mb=4069,8192"))
      << "memory_mb flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSdCardFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--use_sdcard=true,true"))
      << "use_sdcard flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSdCardFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
        },
        {
            "vm": {
                "use_sdcard": false
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--use_sdcard=true,false"))
      << "use_sdcard flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSdCardFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "use_sdcard": false
            }
        },
        {
            "vm": {
                "use_sdcard": false
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--use_sdcard=false,false"))
      << "use_sdcard flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesVmManagerFlagEmptyJson) {
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--vm_manager=crosvm,crosvm)"))
      << "vm_manager flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesVmManagerFlagPartialJson) {
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
                "gem5":{
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--vm_manager=crosvm,gem5)"))
      << "vm_manager flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesVmManagerFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "qemu":{
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--vm_manager=qemu_cli,crosvm)"))
      << "vm_manager flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesVmManagerFlagDefault) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--vm_manager=crosvm,crosvm)"))
      << "vm_manager flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseOneInstanceSetupWizardInvalidValue) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                },
                "setupwizard_mode": "ENABLED"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_TRUE(ParseJsonString(json_text, json_configs));
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_FALSE(serialized_data.ok());
}

TEST(VmFlagsParserTest, ParseTwoInstancesSetupWizardFlagEmptyJson) {
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--setupwizard_mode=DISABLED,DISABLED)"))
      << "setupwizard_mode flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSetupWizardFlagPartialJson) {
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
                },
                "setupwizard_mode": "REQUIRED"
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
      FindConfig(*serialized_data, R"(--setupwizard_mode=DISABLED,REQUIRED)"))
      << "setupwizard_mode flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSetupWizardFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                },
                "setupwizard_mode": "OPTIONAL"
            }
        },
        {
            "vm": {
                "crosvm":{
                },
                "setupwizard_mode": "REQUIRED"
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
      FindConfig(*serialized_data, R"(--setupwizard_mode=OPTIONAL,REQUIRED)"))
      << "setupwizard_mode flag is missing or wrongly formatted";
}

#ifndef GENERATE_MVP_FLAGS_ONLY
TEST(VmFlagsParserTest, ParseTwoInstancesUuidFlagEmptyJson) {
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--uuid=699acfc4-c8c4-11e7-882b-5065f31dc101,699acfc4-c8c4-11e7-882b-5065f31dc101)"))
      << "uuid flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesUuidFlagPartialJson) {
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
                },
                "uuid": "870acfc4-c8c4-11e7-99ac-5065f31dc250"
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
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--uuid=699acfc4-c8c4-11e7-882b-5065f31dc101,870acfc4-c8c4-11e7-99ac-5065f31dc250)"))
      << "uuid flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesUuidFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                },
                "uuid": "870acfc4-c8c4-11e7-99ac-5065f31dc250"
            }
        },
        {
            "vm": {
                "crosvm":{
                },
                "uuid": "870acfc4-c8c4-11e7-99ac-5065f31dc251"
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
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--uuid=870acfc4-c8c4-11e7-99ac-5065f31dc250,870acfc4-c8c4-11e7-99ac-5065f31dc251)"))
      << "uuid flag is missing or wrongly formatted";
}
#endif

TEST(VmFlagsParserTest, ParseTwoInstancesSandboxFlagEmptyJson) {
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--enable_sandbox=false,false)"))
      << "enable_sandbox flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSandboxFlagPartialJson) {
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
                    "enable_sandbox": true
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--enable_sandbox=false,true)"))
      << "enable_sandbox flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSandboxFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                    "enable_sandbox": true
                }
            }
        },
        {
            "vm": {
                "crosvm":{
                    "enable_sandbox": true
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--enable_sandbox=true,true)"))
      << "enable_sandbox flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesCustomActionsFlagEmptyJson) {
  const char* test_string = R""""(
{
    "instances" :
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
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--custom_actions=unset)"))
      << "custom_actions flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesCustomActionsFlagPartialJson) {
  const char* test_string = R""""(
{
        "instances" :
        [
            {
            },
            {
                "vm": {
                        "custom_actions" : [
                                {
                                        "device_states": [
                                                {
                                                        "lid_switch_open": false,
                                                        "hinge_angle_value": 0
                                                }
                                        ]
                                }
                        ]
                }
            }
        ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  std::string expected_custom_actions =
      R"""(--custom_actions=[{\"device_states\":[{\"hinge_angle_value\":0,\"lid_switch_open\":false}]}])""";

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(
      FindConfigIgnoreSpaces(*serialized_data, R"(--custom_actions=unset)"))
      << "custom_actions flag is missing or wrongly formatted";

  EXPECT_TRUE(FindConfigIgnoreSpaces(*serialized_data, expected_custom_actions))
      << "custom_actions flag is missing or wrongly formatted";
}

}  // namespace cuttlefish

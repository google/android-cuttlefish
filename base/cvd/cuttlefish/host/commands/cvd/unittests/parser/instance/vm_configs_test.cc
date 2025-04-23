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

#include <android-base/file.h>
#include <gtest/gtest.h>

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/host/commands/cvd/unittests/parser/test_common.h"

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

TEST(VmFlagsParserTest, ParseTwoInstancesQemu) {
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
                "qemu":{
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--vm_manager=qemu_cli,qemu_cli"))
      << "vm_manager flag is missing or wrongly formatted";
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
                "memory_mb": 4096
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--memory_mb=2048,4096"))
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
                "memory_mb": 4096
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--memory_mb=4096,8192"))
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

TEST(VmFlagsParserTest, ParseTwoInstancesSimpleMediaDeviceFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--crosvm_simple_media_device=false,false)"))
      << "crosvm_simple_media_device flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSimpleMediaDeviceFlagPartialJson) {
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
                    "simple_media_device": true
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--crosvm_simple_media_device=false,true)"))
      << "crosvm_simple_media_device flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesSimpleMediaDeviceFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                    "simple_media_device": true
                }
            }
        },
        {
            "vm": {
                "crosvm":{
                    "simple_media_device": true
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--crosvm_simple_media_device=true,true)"))
      << "crosvm_simple_media_device flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesV4l2ProxyFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--crosvm_v4l2_proxy=,)"))
      << "crosvm_v4l2_proxy is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesV4l2ProxyFlagPartialJson) {
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
                    "v4l2_proxy": "/dev/video0"
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--crosvm_v4l2_proxy=,/dev/video0)"))
      << "crosvm_v4l2_proxy flag is missing or wrongly formatted";
}

TEST(VmFlagsParserTest, ParseTwoInstancesV4l2ProxyFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                    "v4l2_proxy": "/dev/video0"
                }
            }
        },
        {
            "vm": {
                "crosvm":{
                    "v4l2_proxy": "/dev/video1"
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--crosvm_v4l2_proxy=/dev/video0,/dev/video1)"))
      << "crosvm_v4l2_proxy flag is missing or wrongly formatted";
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

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();

  std::vector<std::string> custom_actions;
  for (const auto& flag : *serialized_data) {
    static constexpr char kPrefix[] = "--custom_actions=";
    if (flag.find(kPrefix) == 0) {
      custom_actions.emplace_back(flag.substr(sizeof(kPrefix) - 1));
    }
  }
  std::sort(custom_actions.begin(), custom_actions.end());

  EXPECT_EQ(custom_actions.size(), 2);
  EXPECT_EQ(custom_actions[1], "unset");

  Json::Value expected_actions;
  expected_actions[0]["device_states"][0]["lid_switch_open"] = false;
  expected_actions[0]["device_states"][0]["hinge_angle_value"] = 0;
  EXPECT_THAT(ParseJson(custom_actions[0]), IsOkAndValue(expected_actions));
}

}  // namespace cuttlefish

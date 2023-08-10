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
#include "host/commands/cvd/parser/launch_cvd_parser.h"
#include "host/commands/cvd/unittests/parser/test_common.h"

namespace cuttlefish {

TEST(BootFlagsParserTest, ParseTwoInstancesBootAnimationFlagEmptyJson) {
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
      FindConfig(*serialized_data, R"(--enable_bootanimation=true,true)"))
      << "enable_bootanimation flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesBootAnimationFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                }
            },
            "boot": {
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
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
            "vm": {
                "crosvm":{
                }
            },
            "boot": {
                "enable_bootanimation": false
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
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
            "vm": {
                "crosvm":{
                }
            },
            "security": {
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
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
            "vm": {
                "crosvm":{
                }
            },
            "security": {
                "serial_number": "CUTTLEFISHCVD101"
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(
      *serialized_data, R"(--serial_number=CUTTLEFISHCVD101,CUTTLEFISHCVD102)"))
      << "serial_number flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesEnforceSecurityFlagEmptyJson) {
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
      FindConfig(*serialized_data, R"(--guest_enforce_security=true,true)"))
      << "guest_enforce_security flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesEnforceSecurityFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                }
            },
            "security": {
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
            "security": {
                "guest_enforce_security": false
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
      FindConfig(*serialized_data, R"(--guest_enforce_security=true,false)"))
      << "guest_enforce_security flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesEnforceSecurityFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                }
            },
            "security": {
                "guest_enforce_security": false
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
            "security": {
                "guest_enforce_security": false
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
      FindConfig(*serialized_data, R"(--guest_enforce_security=false,false)"))
      << "guest_enforce_security flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesKernelCmdFlagEmptyJson) {
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--extra_kernel_cmdline=,)"))
      << "extra_kernel_cmdline flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesKernelCmdFlagPartialJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "vm": {
                "crosvm":{
                }
            },
            "boot": {
                "kernel": {
                }
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
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
            "vm": {
                "crosvm":{
                }
            },
            "boot": {
                "kernel": {
                    "extra_kernel_cmdline": "androidboot.selinux=permissive"
                }
            }
        },
        {
            "vm": {
                "crosvm":{
                }
            },
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
  auto serialized_data = LaunchCvdParserTester(json_configs);
  EXPECT_TRUE(serialized_data.ok()) << serialized_data.error().Trace();
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--extra_kernel_cmdline=androidboot.selinux=permissive,lpm_levels.sleep_disabled=1)"))
      << "extra_kernel_cmdline flag is missing or wrongly formatted";
}

}  // namespace cuttlefish

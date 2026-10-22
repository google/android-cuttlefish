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

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

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
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_FALSE(FindConfig(*serialized_data, R"(--blank_data_image_mb=)"))
      << "blank_data_image_mb flag is set";
  EXPECT_FALSE(FindConfig(*serialized_data, R"(--data_policy=)"))
      << "data_policy flag is set";
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
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--blank_data_image_mb=unset,2048)"))
      << "blank_data_image_mb flag is missing or wrongly formatted";
  EXPECT_TRUE(FindConfig(*serialized_data,
                         R"(--data_policy=use_existing,always_create)"))
      << "data_policy flag is missing or wrongly formatted";
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
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--blank_data_image_mb=2048,4096)"))
      << "blank_data_image_mb flag is missing or wrongly formatted";
  EXPECT_TRUE(FindConfig(*serialized_data,
                         R"(--data_policy=always_create,always_create)"))
      << "data_policy flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, VendorBootRemoteOverrideEmitsNoVendorBootImageFlag) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "vendor_boot": "@ab/git_main/cf_x86_64_auto{vendor_boot-debug.img}"
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
  const std::vector<std::string> serialized_data =
      CF_ASSERT(LaunchCvdParserTester(json_configs));
  EXPECT_FALSE(FindConfig(serialized_data, "--vendor_boot_image="));
}

TEST(BootFlagsParserTest, VendorBootLocalOverrideSingleInstance) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "vendor_boot": "/path/to/vendor_boot.img"
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
  const std::vector<std::string> serialized_data =
      CF_ASSERT(LaunchCvdParserTester(json_configs));
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--vendor_boot_image=/path/to/vendor_boot.img"));
}

TEST(BootFlagsParserTest, VendorBootLocalOverrideMultiInstancePartial) {
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
                "disk_artifacts": {
                    "vendor_boot": "/path/to/vendor_boot.img"
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
  const std::vector<std::string> serialized_data =
      CF_ASSERT(LaunchCvdParserTester(json_configs));
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--vendor_boot_image=,/path/to/vendor_boot.img"));
}

TEST(BootFlagsParserTest, VendorBootLocalOverrideMultiInstanceFull) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "vendor_boot": "/path/to/first/vendor_boot.img"
                }
            }
        },
        {
            "disk": {
                "disk_artifacts": {
                    "vendor_boot": "/path/to/second/vendor_boot.img"
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
  const std::vector<std::string> serialized_data =
      CF_ASSERT(LaunchCvdParserTester(json_configs));
  EXPECT_TRUE(FindConfig(
      serialized_data,
      "--vendor_boot_image=/path/to/first/vendor_boot.img,/path/to/second/"
      "vendor_boot.img"));
}

}  // namespace cuttlefish

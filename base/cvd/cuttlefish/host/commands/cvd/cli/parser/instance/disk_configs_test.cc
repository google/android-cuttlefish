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

#include "fmt/format.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
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

TEST(BootFlagsParserTest, DiceHandoverUnsetEmitsNoCrosvmFlags) {
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
  EXPECT_FALSE(FindConfig(serialized_data, "--crosvm_acpi_table="));
  EXPECT_FALSE(FindConfig(serialized_data, "--crosvm_device_tree_overlay="));
  EXPECT_FALSE(
      FindConfig(serialized_data, "--crosvm_file_backed_mapping_base64="));
}

TEST(BootFlagsParserTest, DiceHandoverAcpiTableSingleInstance) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "dice_handover_instance1",
                        "addr": "0x9D1C3000",
                        "acpi_table": "acpi_table_dice.img"
                    }
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
  EXPECT_TRUE(
      FindConfig(serialized_data, "--crosvm_acpi_table=acpi_table_dice.img"));
  EXPECT_FALSE(FindConfig(serialized_data, "--crosvm_device_tree_overlay="));
  // base64("path=dice_handover_instance1,addr=0x9D1C3000,size=0x10000")
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--crosvm_file_backed_mapping_base64="
                         "cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQ"
                         "zMwMDAsc2l6ZT0weDEwMDAw"));
}

TEST(BootFlagsParserTest, DiceHandoverDeviceTreeOverlaySingleInstance) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "dice_handover_instance1",
                        "addr": "0x9D1C3000",
                        "device_tree_overlay": "device_tree_dice.img"
                    }
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
                         "--crosvm_device_tree_overlay=device_tree_dice.img"));
  EXPECT_FALSE(FindConfig(serialized_data, "--crosvm_acpi_table="));
  // base64("path=dice_handover_instance1,addr=0x9D1C3000,size=0x10000")
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--crosvm_file_backed_mapping_base64="
                         "cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQ"
                         "zMwMDAsc2l6ZT0weDEwMDAw"));
}

TEST(BootFlagsParserTest, DiceHandoverTwoInstancesFull) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "dice_handover_instance1",
                        "addr": "0x9D1C3000",
                        "acpi_table": "acpi_table_dice.img"
                    }
                }
            }
        },
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "/abs/dice_handover_instance2",
                        "addr": "0x9d1d3000",
                        "acpi_table": "acpi_table_dice.img"
                    }
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
      "--crosvm_acpi_table=acpi_table_dice.img,acpi_table_dice.img"));
  EXPECT_FALSE(FindConfig(serialized_data, "--crosvm_device_tree_overlay="));
  // base64("path=dice_handover_instance1,addr=0x9D1C3000,size=0x10000"),
  // base64("path=/abs/dice_handover_instance2,addr=0x9d1d3000,size=0x10000")
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--crosvm_file_backed_mapping_base64="
                         "cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQ"
                         "zMwMDAsc2l6ZT0weDEwMDAw,"
                         "cGF0aD0vYWJzL2RpY2VfaGFuZG92ZXJfaW5zdGFuY2UyLGFkZHI9M"
                         "Hg5ZDFkMzAwMCxzaXplPTB4MTAwMDA="));
}

TEST(BootFlagsParserTest, DiceHandoverTwoInstancesPartial) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                }
            }
        },
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "dice_handover_instance1",
                        "addr": "0x9D1C3000",
                        "device_tree_overlay": "device_tree_dice.img"
                    }
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
                         "--crosvm_device_tree_overlay=,device_tree_dice.img"));
  EXPECT_FALSE(FindConfig(serialized_data, "--crosvm_acpi_table="));
  // base64("path=dice_handover_instance1,addr=0x9D1C3000,size=0x10000")
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--crosvm_file_backed_mapping_base64=,"
                         "cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQ"
                         "zMwMDAsc2l6ZT0weDEwMDAw"));
}

TEST(BootFlagsParserTest, DiceHandoverMixedFirmwareTables) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "dice_handover_instance1",
                        "addr": "0x9D1C3000",
                        "acpi_table": "acpi_table_dice.img"
                    }
                }
            }
        },
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "/abs/dice_handover_instance2",
                        "addr": "0x9d1d3000",
                        "device_tree_overlay": "device_tree_dice.img"
                    }
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
  EXPECT_TRUE(
      FindConfig(serialized_data, "--crosvm_acpi_table=acpi_table_dice.img,"));
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--crosvm_device_tree_overlay=,device_tree_dice.img"));
  EXPECT_TRUE(FindConfig(serialized_data,
                         "--crosvm_file_backed_mapping_base64="
                         "cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQ"
                         "zMwMDAsc2l6ZT0weDEwMDAw,"
                         "cGF0aD0vYWJzL2RpY2VfaGFuZG92ZXJfaW5zdGFuY2UyLGFkZHI9M"
                         "Hg5ZDFkMzAwMCxzaXplPTB4MTAwMDA="));
}

TEST(BootFlagsParserTest, DiceHandoverRemoteBuildEmitsCrosvmFlags) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "@ab/git_main/cf_x86_64_auto{dice_handover_instance1}",
                        "addr": "0x9D1C3000",
                        "acpi_table": "acpi_table_dice.img"
                    }
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
  EXPECT_TRUE(
      FindConfig(serialized_data, "--crosvm_acpi_table=acpi_table_dice.img"));
  EXPECT_FALSE(FindConfig(serialized_data, "--crosvm_device_tree_overlay="));
  // base64("path=dice_handover,addr=0x9D1C3000,size=0x10000")
  EXPECT_TRUE(
      FindConfig(serialized_data,
                 "--crosvm_file_backed_mapping_base64="
                 "cGF0aD1kaWNlX2hhbmRvdmVyLGFkZHI9MHg5RDFDMzAwMCxzaXplPT"
                 "B4MTAwMDA="));
}

TEST(BootFlagsParserTest, DiceHandoverRemoteBuildIsValidated) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "dice_handover": {
                        "build": "@ab/git_main/cf_x86_64_auto{dice_handover_instance1}",
                        "addr": "9D1C3000",
                        "acpi_table": "acpi_table_dice.img"
                    }
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
  EXPECT_THAT(LaunchCvdParserTester(json_configs), IsError());
}

class BootFlagsParserInvalidDiceHandoverTest
    : public ::testing::TestWithParam<std::string> {};

TEST_P(BootFlagsParserInvalidDiceHandoverTest, RejectsInvalidDiceHandover) {
  Json::Value json_configs;
  std::string json_text = fmt::format(R""""(
{{
    "instances": [{{
        "disk": {{
            "disk_artifacts": {{
                "dice_handover": {{ {} }}
            }}
        }}
    }}]
}}
  )"""",
                                      GetParam());

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  EXPECT_THAT(LaunchCvdParserTester(json_configs), IsError());
}

INSTANTIATE_TEST_SUITE_P(
    BootFlagsParserTest, BootFlagsParserInvalidDiceHandoverTest,
    ::testing::Values(
        // Missing build.
        R"("addr": "0x9D1C3000", "acpi_table": "a.img")",
        // Empty build.
        R"("build": "", "addr": "0x9D1C3000", "acpi_table": "a.img")",
        // Comma in build.
        R"("build": "a,b", "addr": "0x9D1C3000", "acpi_table": "a.img")",
        // Missing addr.
        R"("build": "a", "acpi_table": "a.img")",
        // Empty addr.
        R"("build": "a", "addr": "", "acpi_table": "a.img")",
        // addr without 0x prefix.
        R"("build": "a", "addr": "9D1C3000", "acpi_table": "a.img")",
        // addr with only the 0x prefix.
        R"("build": "a", "addr": "0x", "acpi_table": "a.img")",
        // addr with non-hexadecimal digits.
        R"("build": "a", "addr": "0x9D1G3000", "acpi_table": "a.img")",
        // addr with uppercase prefix.
        R"("build": "a", "addr": "0X9D1C3000", "acpi_table": "a.img")",
        // addr with extra crosvm options.
        R"("build": "a", "addr": "0x9D1C3000,rw", "acpi_table": "a.img")",
        // Neither acpi_table nor device_tree_overlay.
        R"("build": "a", "addr": "0x9D1C3000")",
        // Both acpi_table and device_tree_overlay.
        R"("build": "a", "addr": "0x9D1C3000", "acpi_table": "a.img",)"
        R"( "device_tree_overlay": "b.img")",
        // Empty acpi_table.
        R"("build": "a", "addr": "0x9D1C3000", "acpi_table": "")",
        // Empty device_tree_overlay.
        R"("build": "a", "addr": "0x9D1C3000", "device_tree_overlay": "")",
        // Comma in acpi_table.
        R"("build": "a", "addr": "0x9D1C3000", "acpi_table": "a,b.img")",
        // Comma in device_tree_overlay.
        R"("build": "a", "addr": "0x9D1C3000",)"
        R"( "device_tree_overlay": "a,b.img")"));

}  // namespace cuttlefish

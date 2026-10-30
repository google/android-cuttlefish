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

TEST(BootFlagsParserTest, ParseTwoInstancesCrosvmAcpiTableFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "acpi_table": "product/etc/opendice_a.aml"
                }
            }
        },
        {
            "disk": {
                "disk_artifacts": {
                    "acpi_table": "product/etc/opendice_b.aml"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--crosvm_acpi_table=product/etc/opendice_a.aml,product/etc/opendice_b.aml)"))
      << "crosvm_acpi_table flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesCrosvmAcpiTableFlagPartialJson) {
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
                    "acpi_table": "product/etc/opendice_b.aml"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data,
                         R"(--crosvm_acpi_table=,product/etc/opendice_b.aml)"))
      << "crosvm_acpi_table flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseOneInstanceCrosvmAcpiTableAbsolutePath) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "acpi_table": "/abs/opendice.aml"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  // An already-absolute value must reach assemble_cvd byte-identical.
  EXPECT_TRUE(
      FindConfig(*serialized_data, R"(--crosvm_acpi_table=/abs/opendice.aml)"))
      << "crosvm_acpi_table flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest,
     ParseTwoInstancesCrosvmDeviceTreeOverlayFlagFullJson) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "device_tree_overlay": {
                        "build": "product/etc/a.dtbo"
                    }
                }
            }
        },
        {
            "disk": {
                "disk_artifacts": {
                    "device_tree_overlay": {
                        "build": "product/etc/b.dtbo"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--crosvm_device_tree_overlay=product/etc/a.dtbo,product/etc/b.dtbo)"))
      << "crosvm_device_tree_overlay flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest,
     ParseTwoInstancesCrosvmDeviceTreeOverlayFlagPartialJson) {
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
                    "device_tree_overlay": {
                        "build": "product/etc/b.dtbo"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data,
                         R"(--crosvm_device_tree_overlay=,product/etc/b.dtbo)"))
      << "crosvm_device_tree_overlay flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseOneInstanceCrosvmFileBackedMappingBase64) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "file_backed_mapping": {
                        "build": "dice_handover_instance1",
                        "addr": "0x9D1C3000",
                        "size": "0x10000"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());

  // base64("path=dice_handover_instance1,addr=0x9D1C3000,size=0x10000") =>
  // cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQzMwMDAsc2l6ZT0weDEwMDAw
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--crosvm_file_backed_mapping_base64=cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQzMwMDAsc2l6ZT0weDEwMDAw)"))
      << "file_backed_mapping flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseTwoInstancesCrosvmFileBackedMappingBase64) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "file_backed_mapping": {
                        "build": "dice_handover_instance1",
                        "addr": "0x9D1C3000",
                        "size": "0x10000"
                    }
                }
            }
        },
        {
            "disk": {
                "disk_artifacts": {
                    "file_backed_mapping": {
                        "build": "dice_handover_instance2",
                        "addr": "0x9D1D3000",
                        "size": "0x20000"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());

  // base64("path=dice_handover_instance1,addr=0x9D1C3000,size=0x10000") =>
  // cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQzMwMDAsc2l6ZT0weDEwMDAw
  // base64("path=dice_handover_instance2,addr=0x9D1D3000,size=0x20000") =>
  // cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMixhZGRyPTB4OUQxRDMwMDAsc2l6ZT0weDIwMDAw
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--crosvm_file_backed_mapping_base64=)"
      R"(cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMSxhZGRyPTB4OUQxQzMwMDAsc2l6ZT0weDEwMDAw,)"
      R"(cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMixhZGRyPTB4OUQxRDMwMDAsc2l6ZT0weDIwMDAw)"))
      << "file_backed_mapping flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest,
     ParseTwoInstancesCrosvmFileBackedMappingPartialJsonBase64) {
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
                    "file_backed_mapping": {
                        "build": "dice_handover_instance2",
                        "addr": "0x9D1D3000",
                        "size": "0x20000"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());

  // base64("path=dice_handover_instance2,addr=0x9D1D3000,size=0x20000") =>
  // cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMixhZGRyPTB4OUQxRDMwMDAsc2l6ZT0weDIwMDAw
  EXPECT_TRUE(FindConfig(
      *serialized_data,
      R"(--crosvm_file_backed_mapping_base64=,)"
      R"(cGF0aD1kaWNlX2hhbmRvdmVyX2luc3RhbmNlMixhZGRyPTB4OUQxRDMwMDAsc2l6ZT0weDIwMDAw)"))
      << "file_backed_mapping flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, RejectsEmptyCrosvmAcpiTable) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "acpi_table": ""
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

TEST(BootFlagsParserTest, RejectsEmptyCrosvmDeviceTreeOverlayBuild) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "device_tree_overlay": {
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

TEST(BootFlagsParserTest, RejectsEmptyStringCrosvmDeviceTreeOverlayBuild) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "device_tree_overlay": {
                        "build": ""
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

TEST(BootFlagsParserTest, ParseOneInstanceCrosvmDeviceTreeOverlayFlag) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "device_tree_overlay": {
                        "build": "product/etc/my_overlay.dtbo"
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
  const Result<std::vector<std::string>> serialized_data =
      LaunchCvdParserTester(json_configs);
  ASSERT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(
      FindConfig(*serialized_data,
                 R"(--crosvm_device_tree_overlay=product/etc/my_overlay.dtbo)"))
      << "crosvm_device_tree_overlay flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, RejectsEmptyCrosvmFileBackedMappingBuild) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "file_backed_mapping": {
                        "addr": "0x9D1C3000",
                        "size": "0x10000"
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

TEST(BootFlagsParserTest, RejectsEmptyStringCrosvmFileBackedMappingBuild) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "file_backed_mapping": {
                        "build": "",
                        "addr": "0x9D1C3000",
                        "size": "0x10000"
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

TEST(BootFlagsParserTest, RejectsEmptyCrosvmFileBackedMappingAddr) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "file_backed_mapping": {
                        "build": "a",
                        "size": "0x10000"
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

TEST(BootFlagsParserTest, RejectsEmptyCrosvmFileBackedMappingSize) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
            "disk": {
                "disk_artifacts": {
                    "file_backed_mapping": {
                        "build": "a",
                        "addr": "0x9D1C3000"
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

class BootFlagsParserInvalidFileBackedMappingTest
    : public ::testing::TestWithParam<std::string> {};

TEST_P(BootFlagsParserInvalidFileBackedMappingTest,
       RejectsCommaInCrosvmFileBackedMappingFields) {
  Json::Value json_configs;
  std::string json_text = GetParam();

  EXPECT_TRUE(ParseJsonString(json_text, json_configs))
      << "Invalid Json string";
  EXPECT_THAT(LaunchCvdParserTester(json_configs), IsError());
}

INSTANTIATE_TEST_SUITE_P(BootFlagsParserTest,
                         BootFlagsParserInvalidFileBackedMappingTest,
                         ::testing::Values(
                             R""""(
{
    "instances": [{
        "disk": {
            "disk_artifacts": {
                "file_backed_mapping": {
                    "build": "a,b",
                    "addr": "0x9D1C3000",
                    "size": "0x10000"
                }
            }
        }
    }]
}
      )"""",
                             R""""(
{
    "instances": [{
        "disk": {
            "disk_artifacts": {
                "file_backed_mapping": {
                    "build": "a",
                    "addr": "0x9D1C3000,rw",
                    "size": "0x10000"
                }
            }
        }
    }]
}
      )"""",
                             R""""(
{
    "instances": [{
        "disk": {
            "disk_artifacts": {
                "file_backed_mapping": {
                    "build": "a",
                    "addr": "0x9D1C3000",
                    "size": "0x10000,rw"
                }
            }
        }
    }]
}
      )""""));

}  // namespace cuttlefish

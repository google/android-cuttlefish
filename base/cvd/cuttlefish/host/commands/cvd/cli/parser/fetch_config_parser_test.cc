/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/parser/fetch_config_parser.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "json/value.h"

#include "cuttlefish/host/commands/cvd/cli/parser/cf_flags_validator.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

Result<std::vector<std::string>> FetchCvdParserTester(const Json::Value& root) {
  auto config = CF_EXPECT(ValidateCfConfigs(root), "Json validation failed");
  return ParseFetchCvdConfigs(config, "/tmp/fetch_test", {"0"});
}

}  // namespace

using ::testing::AllOf;
using ::testing::HasSubstr;

TEST(FetchConfigParserTests, AndroidEfiLoaderBuildOnlySuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "boot": {
                "android_efi_loader": {
                    "build": "@ab/branch/target"
                }
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(FindConfig(*flags, "--android_efi_loader_build=branch/target"));
}

TEST(FetchConfigParserTests, KernelBuildOnlySuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "boot": {
                "kernel": {
                    "build": "@ab/branch/target"
                }
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(FindConfig(*flags, "--kernel_build=branch/target"));
}

TEST(FetchConfigParserTests, NoBuildStringsProducesNoFlagsSuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "vm": {
                "memory_mb": 4096
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(flags->empty());
}

TEST(FetchConfigParserTests, MalformedAbBuildStringFail) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "default_build": "@ab/branch/target/extra"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  EXPECT_THAT(FetchCvdParserTester(json_configs), IsError());
}

TEST(FetchConfigParserTests, ObjectUrlBuildsSuccess) {
  const char* test_string = R""""(
{
    "common": {
        "host_package": "gs://bucket/dist/cvd-host_package.tar.gz"
    },
    "instances": [
        {
            "disk": {
                "default_build": "gs://bucket/dist/phone-img-1.zip"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(
      FindConfig(*flags, "--default_build=gs://bucket/dist/phone-img-1.zip"));
  EXPECT_TRUE(FindConfig(
      *flags, "--host_package_build=gs://bucket/dist/cvd-host_package.tar.gz"));
}

TEST(FetchConfigParserTests, DirectoryUrlBuildsSuccess) {
  const char* test_string = R""""(
{
    "common": {
        "host_package": "gs://bucket/host/"
    },
    "instances": [
        {
            "disk": {
                "default_build": "gs://bucket/dist/"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(FindConfig(*flags, "--default_build=gs://bucket/dist/"));
  EXPECT_TRUE(FindConfig(*flags, "--host_package_build=gs://bucket/host/"));
}

TEST(FetchConfigParserTests, HttpsDirectoryUrlBuildSuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "default_build": "https://example.com/dist/"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(FindConfig(*flags, "--default_build=https://example.com/dist/"));
}

TEST(FetchConfigParserTests, OtaToolsUrlBuildSuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "otatools": "gs://bucket/dist/"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(FindConfig(*flags, "--otatools_build=gs://bucket/dist/"));
}

TEST(FetchConfigParserTests, UrlFilepathSelectorSuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "boot": {
                "build": "gs://bucket/dist/images.zip{boot.img}"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(
      FindConfig(*flags, "--boot_build=gs://bucket/dist/images.zip{boot.img}"));
}

TEST(FetchConfigParserTests, CleartextHttpUrlFail) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "default_build": "http://example.com/dist/"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  EXPECT_THAT(
      FetchCvdParserTester(json_configs),
      IsErrorAndMessage(AllOf(HasSubstr("http://"), HasSubstr("https://"))));
}

TEST(FetchConfigParserTests, UnsupportedUrlSchemeFail) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "default_build": "s3://bucket/dist/"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  EXPECT_THAT(FetchCvdParserTester(json_configs),
              IsErrorAndMessage(AllOf(HasSubstr("s3"), HasSubstr("gs://"))));
}

// The build flags are comma separated, so a comma has to be refused here,
// while the whole value is still one string.
TEST(FetchConfigParserTests, UrlWithCommaFail) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "default_build": "https://example.com/d/img.zip?k=a,b"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  EXPECT_THAT(FetchCvdParserTester(json_configs),
              IsErrorAndMessage(HasSubstr("comma")));
}

TEST(FetchConfigParserTests, LocalPathProducesNoFlagsSuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "default_build": "/home/user/local_images"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(flags->empty());
}

TEST(FetchConfigParserTests, UnprefixedRelativeValuesProduceNoFlagsSuccess) {
  const char* test_string = R""""(
{
    "instances": [
        {
            "disk": {
                "default_build": "out/target/product/vsoc_x86_64",
                "otatools": "branch/target"
            }
        }
    ]
}
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);
  ASSERT_TRUE(ParseJsonString(json_text, json_configs));

  Result<std::vector<std::string>> flags = FetchCvdParserTester(json_configs);
  ASSERT_THAT(flags, IsOk());
  EXPECT_TRUE(flags->empty());
}

}  // namespace cuttlefish

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
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

Result<std::vector<std::string>> FetchCvdParserTester(const Json::Value& root) {
  const cvd::config::EnvironmentSpecification config =
      CF_EXPECT(ValidateCfConfigs(root), "Json validation failed");
  return ParseFetchCvdConfigs(config, "/tmp/fetch_test", {"0"});
}

}  // namespace

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

}  // namespace cuttlefish

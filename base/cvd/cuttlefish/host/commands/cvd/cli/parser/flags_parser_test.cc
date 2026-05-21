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
#include <sstream>
#include <string>

#include <android-base/file.h>
#include <gtest/gtest.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"

namespace cuttlefish {

TEST(FlagsParserTest, ParseInvalidJson) {
  const char* test_string = R""""(
    instances=50;
  )"""";

  Json::Value json_configs;
  std::string json_text(test_string);

  EXPECT_FALSE(ParseJsonString(json_text, json_configs));
}

TEST(FlagsParserTest, ParseJsonWithSpellingError) {
  const char* test_string = R""""(
{
    "Insta" :
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
  EXPECT_FALSE(serialized_data.ok());
}

TEST(FlagsParserTest, ParseBasicJsonSingleInstances) {
  const char* test_string = R""""(
{
    "instances" :
    [
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--num_instances=1"))
      << "num_instances flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseBasicJsonTwoInstances) {
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--num_instances=2"))
      << "num_instances flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseNetSimFlagEmptyJson) {
  const char* test_string = R""""(
{
  "instances" :
  [
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
  EXPECT_FALSE(FindConfig(*serialized_data, R"(--netsim_bt=)"))
      << "netsim_bt flag is set";
  EXPECT_FALSE(FindConfig(*serialized_data, R"(--netsim_uwb=)"))
      << "netsim_uwb flag is set";
}

TEST(BootFlagsParserTest, ParseNetSimFlagEnabled) {
  const char* test_string = R""""(
{
   "netsim_bt": false,
   "netsim_uwb": true,
     "instances" :
     [
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
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--netsim_bt=false)"))
      << "netsim_bt flag is missing or wrongly formatted";
  EXPECT_TRUE(FindConfig(*serialized_data, R"(--netsim_uwb=true)"))
      << "netsim_uwb flag is missing or wrongly formatted";
}

TEST(CvdLoadFlagsTest, CredentialSourceGetterSetter) {
  LoadFlags load_flags;

  // Test Setter
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    std::vector<std::string> args = {"--credential_source=first_val"};
    auto result = ConsumeFlags(flags, args);
    ASSERT_TRUE(result.ok()) << result.error().Trace();

    ASSERT_EQ(load_flags.overrides.size(), 1);
    EXPECT_EQ(load_flags.overrides[0].config_path, "fetch.credential_source");
    EXPECT_EQ(load_flags.overrides[0].new_value, "first_val");
  }

  // Test Getter (should return "first_val" now)
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    auto flag_it = std::find_if(flags.begin(), flags.end(), [](const Flag& f) {
      return f.Name() == "credential_source";
    });
    ASSERT_NE(flag_it, flags.end());

    std::stringstream ss;
    ss << *flag_it;
    EXPECT_NE(ss.str().find("Current value: \"first_val\""), std::string::npos)
        << "Help text was: " << ss.str();
  }

  // Test Setter again (should append and override)
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    std::vector<std::string> args = {"--credential_source=second_val"};
    auto result = ConsumeFlags(flags, args);
    ASSERT_TRUE(result.ok()) << result.error().Trace();

    ASSERT_EQ(load_flags.overrides.size(), 2);
    EXPECT_EQ(load_flags.overrides[1].config_path, "fetch.credential_source");
    EXPECT_EQ(load_flags.overrides[1].new_value, "second_val");
  }

  // Test Getter again (should return "second_val" because it searches from the end)
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    auto flag_it = std::find_if(flags.begin(), flags.end(), [](const Flag& f) {
      return f.Name() == "credential_source";
    });
    ASSERT_NE(flag_it, flags.end());

    std::stringstream ss;
    ss << *flag_it;
    EXPECT_NE(ss.str().find("Current value: \"second_val\""), std::string::npos)
        << "Help text was: " << ss.str();
  }
}

TEST(CvdLoadFlagsTest, ProjectIDGetterSetter) {
  LoadFlags load_flags;

  // Test Setter
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    std::vector<std::string> args = {"--project_id=first_project"};
    auto result = ConsumeFlags(flags, args);
    ASSERT_TRUE(result.ok()) << result.error().Trace();

    ASSERT_EQ(load_flags.overrides.size(), 1);
    EXPECT_EQ(load_flags.overrides[0].config_path, "fetch.project_id");
    EXPECT_EQ(load_flags.overrides[0].new_value, "first_project");
  }

  // Test Getter
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    auto flag_it = std::find_if(flags.begin(), flags.end(), [](const Flag& f) {
      return f.Name() == "project_id";
    });
    ASSERT_NE(flag_it, flags.end());

    std::stringstream ss;
    ss << *flag_it;
    EXPECT_NE(ss.str().find("Current value: \"first_project\""), std::string::npos)
        << "Help text was: " << ss.str();
  }

  // Test Setter again
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    std::vector<std::string> args = {"--project_id=second_project"};
    auto result = ConsumeFlags(flags, args);
    ASSERT_TRUE(result.ok()) << result.error().Trace();

    ASSERT_EQ(load_flags.overrides.size(), 2);
    EXPECT_EQ(load_flags.overrides[1].config_path, "fetch.project_id");
    EXPECT_EQ(load_flags.overrides[1].new_value, "second_project");
  }

  // Test Getter again
  {
    auto flags = BuildCvdLoadFlags(load_flags);
    auto flag_it = std::find_if(flags.begin(), flags.end(), [](const Flag& f) {
      return f.Name() == "project_id";
    });
    ASSERT_NE(flag_it, flags.end());

    std::stringstream ss;
    ss << *flag_it;
    EXPECT_NE(ss.str().find("Current value: \"second_project\""), std::string::npos)
        << "Help text was: " << ss.str();
  }
}

}  // namespace cuttlefish

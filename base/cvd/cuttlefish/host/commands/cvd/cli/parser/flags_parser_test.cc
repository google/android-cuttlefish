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

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result_matchers.h"

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

TEST(BootFlagsParserTest, ParseNetSimArgs) {
  const char* test_string = R""""(
{
   "netsim_args": ["--wifi-instance=1", "--bt-instance=2"],
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

  auto json_configs = ParseJson(test_string);
  EXPECT_THAT(json_configs, IsOk());
  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsOk());
  EXPECT_TRUE(FindConfig(*serialized_data, "--netsim_args=--wifi-instance=1 --bt-instance=2"))
      << "netsim_args flag is missing or wrongly formatted";
}

TEST(BootFlagsParserTest, ParseNetSimArgsWhitespaceError) {
  const char* test_string = R""""(
{
   "netsim_args": ["--wifi-instance=1", "--bt-instance 2"],
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

  auto json_configs = ParseJson(test_string);
  EXPECT_THAT(json_configs, IsOk());
  auto serialized_data = LaunchCvdParserTester(*json_configs);
  EXPECT_THAT(serialized_data, IsError());
}

TEST(CvdLoadFlagsTest, CredentialSourceSetter) {
  LoadFlags load_flags;

  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--credential_source=foo"};
  auto result = ConsumeFlags(flags, args);
  ASSERT_TRUE(result.ok()) << result.error().Trace();

  ASSERT_EQ(load_flags.overrides.size(), 1);
  EXPECT_EQ(load_flags.overrides.count("fetch.credential_source"), 1);
  EXPECT_EQ(load_flags.overrides["fetch.credential_source"], "foo");
}

TEST(CvdLoadFlagsTest, CredentialSourceGetter) {
  LoadFlags load_flags;
  load_flags.overrides["fetch.credential_source"] = "bar";

  auto flags = BuildCvdLoadFlags(load_flags);
  auto flag_it = std::find_if(flags.begin(), flags.end(), [](const Flag& f) {
    return f.Name() == "credential_source";
  });
  ASSERT_NE(flag_it, flags.end());

  std::stringstream ss;
  ss << *flag_it;
  EXPECT_NE(ss.str().find("Current value: \"bar\""), std::string::npos)
      << "Help text was: " << ss.str();
}

TEST(CvdLoadFlagsTest, CredentialSourceDuplicated) {
  LoadFlags load_flags;

  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--credential_source=first_val",
                                   "--credential_source=second_val"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_THAT(result, IsError());
}

TEST(CvdLoadFlagsTest, ProjectIDSetter) {
  LoadFlags load_flags;

  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--project_id=foo"};
  auto result = ConsumeFlags(flags, args);
  ASSERT_TRUE(result.ok()) << result.error().Trace();

  ASSERT_EQ(load_flags.overrides.size(), 1);
  EXPECT_EQ(load_flags.overrides.count("fetch.project_id"), 1);
  EXPECT_EQ(load_flags.overrides["fetch.project_id"], "foo");
}

TEST(CvdLoadFlagsTest, ProjectIDGetter) {
  LoadFlags load_flags;
  load_flags.overrides["fetch.project_id"] = "bar";

  auto flags = BuildCvdLoadFlags(load_flags);
  auto flag_it = std::find_if(flags.begin(), flags.end(), [](const Flag& f) {
    return f.Name() == "project_id";
  });
  ASSERT_NE(flag_it, flags.end());

  std::stringstream ss;
  ss << *flag_it;
  EXPECT_NE(ss.str().find("Current value: \"bar\""),
            std::string::npos)
      << "Help text was: " << ss.str();
}

TEST(CvdLoadFlagsTest, ProjectIDDuplicated) {
  LoadFlags load_flags;

  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--project_id=first_project",
                                   "--project_id=second_project"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.ok()) << "Expected duplicate flag to fail";
}

TEST(CvdLoadFlagsTest, CredentialSourceConflictWithOverride) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--override=fetch.credential_source:override_val",
      "--credential_source=flag_val"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.ok()) << "Expected override followed by flag to fail";
}

TEST(CvdLoadFlagsTest, OverrideConflictWithCredentialSource) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--credential_source=flag_val",
      "--override=fetch.credential_source:override_val"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.ok()) << "Expected flag followed by override to fail";
}

TEST(CvdLoadFlagsTest, ProjectIDConflictWithOverride) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--override=fetch.project_id:override_project",
      "--project_id=flag_project"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.ok()) << "Expected override followed by flag to fail";
}

TEST(CvdLoadFlagsTest, OverrideConflictWithProjectID) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--project_id=flag_project",
      "--override=fetch.project_id:override_project"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.ok()) << "Expected flag followed by override to fail";
}

TEST(CvdLoadFlagsTest, DuplicateOverridesFail) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--override=fetch.credential_source:val1", "--override=fetch.credential_source:val2"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.ok()) << "Expected duplicate overrides to fail";
}

TEST(FlagsParserTest, ParseMediaSplaneSingleInstance) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {}
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--media=type=v4l2_emulated_camera_splane"))
      << "media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaSplaneTwoDevices) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_splane": {}
              },
              {
                "v4l2_emulated_camera_splane": {}
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
  EXPECT_EQ(std::count(serialized_data->begin(), serialized_data->end(),
                       "--media=type=v4l2_emulated_camera_splane"),
            2);
}

TEST(FlagsParserTest, ParseMediaMplane) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_emulated_camera_mplane": {}
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--media=type=v4l2_emulated_camera_mplane"))
      << "media flag is missing or wrongly formatted";
}

TEST(FlagsParserTest, ParseMediaV4l2Proxy) {
  const char* test_string = R""""(
{
    "instances" :
    [
        {
          "media": {
            "devices": [
              {
                "v4l2_proxy": {
                  "device_path": "/dev/video0"
                }
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
  EXPECT_TRUE(FindConfig(*serialized_data, "--media=type=v4l2_proxy"))
      << "media flag is missing or wrongly formatted";
}

}  // namespace cuttlefish

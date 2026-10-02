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

#include "gtest/gtest.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(CvdLoadFlagsTest, CredentialSourceSetter) {
  LoadFlags load_flags;

  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--credential_source=foo"};
  auto result = ConsumeFlags(flags, args);
  ASSERT_THAT(result, IsOk());

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
  ASSERT_THAT(result, IsOk());

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
  EXPECT_NE(ss.str().find("Current value: \"bar\""), std::string::npos)
      << "Help text was: " << ss.str();
}

TEST(CvdLoadFlagsTest, ProjectIDDuplicated) {
  LoadFlags load_flags;

  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--project_id=first_project",
                                   "--project_id=second_project"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.has_value()) << "Expected duplicate flag to fail";
}

TEST(CvdLoadFlagsTest, CredentialSourceConflictWithOverride) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--override=fetch.credential_source:override_val",
      "--credential_source=flag_val"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.has_value())
      << "Expected override followed by flag to fail";
}

TEST(CvdLoadFlagsTest, OverrideConflictWithCredentialSource) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--credential_source=flag_val",
      "--override=fetch.credential_source:override_val"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.has_value())
      << "Expected flag followed by override to fail";
}

TEST(CvdLoadFlagsTest, ProjectIDConflictWithOverride) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--override=fetch.project_id:override_project",
      "--project_id=flag_project"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.has_value())
      << "Expected override followed by flag to fail";
}

TEST(CvdLoadFlagsTest, OverrideConflictWithProjectID) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {
      "--project_id=flag_project",
      "--override=fetch.project_id:override_project"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.has_value())
      << "Expected flag followed by override to fail";
}

TEST(CvdLoadFlagsTest, DuplicateOverridesFail) {
  LoadFlags load_flags;
  auto flags = BuildCvdLoadFlags(load_flags);
  std::vector<std::string> args = {"--override=fetch.credential_source:val1",
                                   "--override=fetch.credential_source:val2"};
  auto result = ConsumeFlags(flags, args);
  EXPECT_FALSE(result.has_value()) << "Expected duplicate overrides to fail";
}

}  // namespace
}  // namespace cuttlefish

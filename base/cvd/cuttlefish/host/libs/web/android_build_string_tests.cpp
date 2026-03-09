//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/libs/web/android_build_string.h"

#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::VariantWith;

TEST(ParseBuildStringTests, DeviceBuildStringSuccess) {
  auto result = ParseBuildString("abcde/test_target");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<DeviceBuildString>(DeviceBuildString{
                  .branch_or_id = "abcde", .target = "test_target"}));

  result = ParseBuildString("12345/test_target");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<DeviceBuildString>(DeviceBuildString{
                  .branch_or_id = "12345", .target = "test_target"}));
}

TEST(ParseBuildStringTests, DeviceBuildStringNoTargetSuccess) {
  auto result = ParseBuildString("abcde");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<DeviceBuildString>(DeviceBuildString{
                  .branch_or_id = "abcde", .target = std::nullopt}));

  result = ParseBuildString("12345");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<DeviceBuildString>(DeviceBuildString{
                  .branch_or_id = "12345", .target = std::nullopt}));
}

TEST(ParseBuildStringTests, DirectoryBuildStringSinglePathSuccess) {
  auto result = ParseBuildString("test_path:test_target");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<DirectoryBuildString>(DirectoryBuildString{
                  .paths = {"test_path"}, .target = "test_target"}));
}

TEST(ParseBuildStringTests, DirectoryBuildStringMultiplePathSuccess) {
  auto result = ParseBuildString("test_path1:test_path2:test_target");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(
      result.value(),
      VariantWith<DirectoryBuildString>(DirectoryBuildString{
          .paths = {"test_path1", "test_path2"}, .target = "test_target"}));
}

TEST(ParseBuildStringTests, EmptyStringFail) {
  auto result = ParseBuildString("");
  EXPECT_THAT(result, IsError());
}

TEST(ParseBuildStringTests, DeviceBuildStringMultipleSlashesFail) {
  auto result = ParseBuildString("abcde/test_target/");
  EXPECT_THAT(result, IsError());

  result = ParseBuildString("12345/test_target/");
  EXPECT_THAT(result, IsError());
}

TEST(ParseBuildStringTests, FilepathExistsSuccess) {
  auto result = ParseBuildString("abcde{filepath}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<DeviceBuildString>(DeviceBuildString{
                  .branch_or_id = "abcde", .filepath = "filepath"}));

  result = ParseBuildString("abcde/target{filepath}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<DeviceBuildString>(
                                  DeviceBuildString{.branch_or_id = "abcde",
                                                    .target = "target",
                                                    .filepath = "filepath"}));
}

TEST(ParseBuildStringTests, FilepathExistsMissingBracketFail) {
  auto result = ParseBuildString("abcde{filepath");
  EXPECT_THAT(result, IsError());

  result = ParseBuildString("abcdefilepath}");
  EXPECT_THAT(result, IsError());
}

TEST(ParseBuildStringTests, FilepathBracketsButNoValueFail) {
  auto result = ParseBuildString("abcde{}");
  EXPECT_THAT(result, IsError());
}

TEST(ParseBuildStringTests, FilepathOnlyFail) {
  auto result = ParseBuildString("{filepath}");
  EXPECT_THAT(result, IsError());
}

TEST(SingleBuildStringGflagsCompatFlagTests, EmptyInputEmptyResultSuccess) {
  std::optional<BuildString> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag="}), IsOk());
  ASSERT_THAT(value, Eq(std::nullopt));
}

TEST(SingleBuildStringGflagsCompatFlagTests, HasValueSuccess) {
  std::optional<BuildString> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag=12345"}), IsOk());
  ASSERT_THAT(value, Optional(DeviceBuildString{.branch_or_id = "12345"}));
  ASSERT_THAT(flag.Parse({"--myflag=abcde/test_target"}), IsOk());
  ASSERT_THAT(value, Optional(DeviceBuildString{.branch_or_id = "abcde",
                                                .target = "test_target"}));
}

TEST(BuildStringGflagsCompatFlagTests, EmptyInputEmptyResultSuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag="}), IsOk());
  ASSERT_THAT(value, IsEmpty());
}

TEST(BuildStringGflagsCompatFlagTests, MultiValueSuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag=12345,abcde"}), IsOk());
  ASSERT_THAT(value, SizeIs(2));
  ASSERT_THAT(value, ElementsAre(DeviceBuildString{.branch_or_id = "12345"},
                                 DeviceBuildString{.branch_or_id = "abcde"}));

  ASSERT_THAT(flag.Parse({"--myflag=12345/test_target,abcde/test_target"}),
              IsOk());
  ASSERT_THAT(value, SizeIs(2));
  ASSERT_THAT(
      value,
      ElementsAre(
          DeviceBuildString{.branch_or_id = "12345", .target = "test_target"},
          DeviceBuildString{.branch_or_id = "abcde", .target = "test_target"}));
}

TEST(BuildStringGflagsCompatFlagTests, MultiEmptyValueSuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag=,"}), IsOk());
  ASSERT_THAT(value, SizeIs(2));
  ASSERT_THAT(value, ElementsAre(std::nullopt, std::nullopt));
}

TEST(BuildStringGflagsCompatFlagTests, MultiValueMixedWithEmptySuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag=12345,,abcde"}), IsOk());
  ASSERT_THAT(value, SizeIs(3));
  ASSERT_THAT(value, ElementsAre(DeviceBuildString{.branch_or_id = "12345"},
                                 std::nullopt,
                                 DeviceBuildString{.branch_or_id = "abcde"}));

  ASSERT_THAT(flag.Parse({"--myflag=12345/test_target,,abcde/test_target"}),
              IsOk());
  ASSERT_THAT(value, SizeIs(3));
  ASSERT_THAT(
      value,
      ElementsAre(
          DeviceBuildString{.branch_or_id = "12345", .target = "test_target"},
          std::nullopt,
          DeviceBuildString{.branch_or_id = "abcde", .target = "test_target"}));
}

TEST(ParseBuildStringTests, GcsBuildStringSuccess) {
  auto result = ParseBuildString("gs://bucket/file.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<GcsBuildString>(
                  GcsBuildString{.url = "gs://bucket/file.zip"}));
}

TEST(ParseBuildStringTests, HttpBuildStringHttpsSuccess) {
  auto result = ParseBuildString("https://example.com/file.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<HttpBuildString>(
                  HttpBuildString{.url = "https://example.com/file.zip"}));
}

TEST(ParseBuildStringTests, HttpBuildStringHttpSuccess) {
  auto result = ParseBuildString("http://host/file.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<HttpBuildString>(
                  HttpBuildString{.url = "http://host/file.zip"}));
}

TEST(ParseBuildStringTests, GcsBuildStringWithFilepathSuccess) {
  auto result = ParseBuildString("gs://bucket/img.zip{boot.img}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<GcsBuildString>(GcsBuildString{
                  .url = "gs://bucket/img.zip", .filepath = "boot.img"}));
}

TEST(ParseBuildStringTests, HttpBuildStringWithFilepathSuccess) {
  auto result = ParseBuildString("https://example.com/img.zip{boot.img}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<HttpBuildString>(HttpBuildString{
                  .url = "https://example.com/img.zip",
                  .filepath = "boot.img"}));
}

TEST(ParseBuildStringTests, GcsBuildStringNotMisparsedAsDirectory) {
  // URLs contain "://" which includes ":" — ensure this is not
  // misinterpreted as a directory build string separator.
  auto result = ParseBuildString("gs://bucket/path/file.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<GcsBuildString>(
                  GcsBuildString{.url = "gs://bucket/path/file.zip"}));
}

TEST(ParseBuildStringTests, HttpBuildStringNotMisparsedAsDirectory) {
  auto result = ParseBuildString("https://example.com/path/file.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<HttpBuildString>(
                  HttpBuildString{.url = "https://example.com/path/file.zip"}));
}

TEST(ParseBuildStringTests, UnsupportedS3SchemeFail) {
  auto result = ParseBuildString("s3://bucket/file");
  // s3:// does not start with gs://, https://, or http://, so it falls
  // through to non-URL parsing. With the ":" it becomes a directory
  // build string, which is allowed but not a URL.
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<DirectoryBuildString>(testing::_));
}

TEST(SingleBuildStringGflagsCompatFlagTests, GcsBuildStringSuccess) {
  std::optional<BuildString> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag=gs://bucket/image.zip"}), IsOk());
  ASSERT_THAT(
      value,
      Optional(VariantWith<GcsBuildString>(
          GcsBuildString{.url = "gs://bucket/image.zip"})));
}

TEST(SingleBuildStringGflagsCompatFlagTests, HttpBuildStringSuccess) {
  std::optional<BuildString> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(flag.Parse({"--myflag=https://example.com/image.zip"}), IsOk());
  ASSERT_THAT(
      value,
      Optional(VariantWith<HttpBuildString>(
          HttpBuildString{.url = "https://example.com/image.zip"})));
}

}  // namespace cuttlefish

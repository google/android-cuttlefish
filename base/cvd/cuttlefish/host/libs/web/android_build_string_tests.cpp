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

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::SizeIs;
using ::testing::VariantWith;

constexpr char kSha256[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

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

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag="}), IsOk());
  ASSERT_THAT(value, Eq(std::nullopt));
}

TEST(SingleBuildStringGflagsCompatFlagTests, HasValueSuccess) {
  std::optional<BuildString> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=12345"}), IsOk());
  ASSERT_THAT(value, Optional(DeviceBuildString{.branch_or_id = "12345"}));
  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=abcde/test_target"}), IsOk());
  ASSERT_THAT(value, Optional(DeviceBuildString{.branch_or_id = "abcde",
                                                .target = "test_target"}));
}

TEST(BuildStringGflagsCompatFlagTests, EmptyInputEmptyResultSuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag="}), IsOk());
  ASSERT_THAT(value, IsEmpty());
}

TEST(BuildStringGflagsCompatFlagTests, MultiValueSuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=12345,abcde"}), IsOk());
  ASSERT_THAT(value, SizeIs(2));
  ASSERT_THAT(value, ElementsAre(DeviceBuildString{.branch_or_id = "12345"},
                                 DeviceBuildString{.branch_or_id = "abcde"}));

  ASSERT_THAT(
      ConsumeFlags({flag}, {"--myflag=12345/test_target,abcde/test_target"}),
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

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=,"}), IsOk());
  ASSERT_THAT(value, SizeIs(2));
  ASSERT_THAT(value, ElementsAre(std::nullopt, std::nullopt));
}

TEST(BuildStringGflagsCompatFlagTests, MultiValueMixedWithEmptySuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=12345,,abcde"}), IsOk());
  ASSERT_THAT(value, SizeIs(3));
  ASSERT_THAT(value, ElementsAre(DeviceBuildString{.branch_or_id = "12345"},
                                 std::nullopt,
                                 DeviceBuildString{.branch_or_id = "abcde"}));

  ASSERT_THAT(
      ConsumeFlags({flag}, {"--myflag=12345/test_target,,abcde/test_target"}),
      IsOk());
  ASSERT_THAT(value, SizeIs(3));
  ASSERT_THAT(
      value,
      ElementsAre(
          DeviceBuildString{.branch_or_id = "12345", .target = "test_target"},
          std::nullopt,
          DeviceBuildString{.branch_or_id = "abcde", .target = "test_target"}));
}

TEST(ParseBuildStringTests, GcsObjectSuccess) {
  auto result = ParseBuildString("gs://bucket/path/file.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<GcsBuildString>(GcsBuildString{
                                  .url = "gs://bucket/path/file.zip"}));
}

TEST(ParseBuildStringTests, GcsDirectorySuccess) {
  auto result = ParseBuildString("gs://bucket/dist/");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<GcsBuildString>(
                                  GcsBuildString{.url = "gs://bucket/dist/"}));
}

TEST(ParseBuildStringTests, GcsObjectFilepathSuccess) {
  auto result = ParseBuildString("gs://bucket/img.zip{boot.img}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<GcsBuildString>(GcsBuildString{
                  .url = "gs://bucket/img.zip", .filepath = "boot.img"}));
}

TEST(ParseBuildStringTests, GcsDirectoryFilepathSuccess) {
  auto result = ParseBuildString("gs://bucket/dist/{bzImage}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<GcsBuildString>(GcsBuildString{
                  .url = "gs://bucket/dist/", .filepath = "bzImage"}));
}

TEST(ParseBuildStringTests, HttpObjectSuccess) {
  auto result = ParseBuildString("https://example.com/path/file.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<HttpBuildString>(HttpBuildString{
                                  .url = "https://example.com/path/file.zip"}));
}

TEST(ParseBuildStringTests, HttpDirectorySuccess) {
  auto result = ParseBuildString("https://example.com/dist/");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<HttpBuildString>(HttpBuildString{
                                  .url = "https://example.com/dist/"}));
}

TEST(ParseBuildStringTests, HttpObjectFilepathSuccess) {
  auto result = ParseBuildString("https://example.com/img.zip{boot.img}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<HttpBuildString>(HttpBuildString{
                                  .url = "https://example.com/img.zip",
                                  .filepath = "boot.img"}));
}

TEST(ParseBuildStringTests, HttpDirectoryFilepathSuccess) {
  auto result = ParseBuildString("https://example.com/dist/{mykernel}");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<HttpBuildString>(HttpBuildString{
                  .url = "https://example.com/dist/", .filepath = "mykernel"}));
}

TEST(ParseBuildStringTests, UrlIsNotADirectoryBuildStringSuccess) {
  auto result = ParseBuildString("gs://bucket/a/b/c.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<GcsBuildString>(GcsBuildString{
                                  .url = "gs://bucket/a/b/c.zip"}));

  result = ParseBuildString("https://example.com:8443/c.zip");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<HttpBuildString>(HttpBuildString{
                                  .url = "https://example.com:8443/c.zip"}));
}

TEST(ParseBuildStringTests, CleartextHttpFail) {
  EXPECT_THAT(ParseBuildString("http://example.com/file.zip"),
              IsErrorAndMessage(HasSubstr("https://")));
}

TEST(ParseBuildStringTests, UnknownSchemeFail) {
  EXPECT_THAT(
      ParseBuildString("s3://bucket/file.zip"),
      IsErrorAndMessage(AllOf(HasSubstr("gs://"), HasSubstr("https://"))));
  EXPECT_THAT(
      ParseBuildString("ftp://example.com/file.zip"),
      IsErrorAndMessage(AllOf(HasSubstr("gs://"), HasSubstr("https://"))));
}

TEST(ParseBuildStringTests, Sha256FragmentSuccess) {
  auto result =
      ParseBuildString(std::string("gs://bucket/file.zip#sha256=") + kSha256);
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<GcsBuildString>(GcsBuildString{
                  .url = "gs://bucket/file.zip", .sha256 = kSha256}));
}

TEST(ParseBuildStringTests, Sha256FragmentAndFilepathSuccess) {
  auto result = ParseBuildString(
      std::string("https://example.com/img.zip{boot.img}#sha256=") + kSha256);
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(), VariantWith<HttpBuildString>(HttpBuildString{
                                  .url = "https://example.com/img.zip",
                                  .filepath = "boot.img",
                                  .sha256 = kSha256}));
}

TEST(ParseBuildStringTests, FragmentBeforeFilepathFail) {
  EXPECT_THAT(ParseBuildString(std::string("gs://bucket/img.zip#sha256=") +
                               kSha256 + "{boot.img}"),
              IsError());
}

TEST(ParseBuildStringTests, MalformedFragmentFail) {
  EXPECT_THAT(ParseBuildString("gs://bucket/file.zip#sha256=abcdef"),
              IsError());
  EXPECT_THAT(ParseBuildString("gs://bucket/file.zip#md5=abcdef"), IsError());
  EXPECT_THAT(
      ParseBuildString(std::string("gs://bucket/fi#le.zip#sha256=") + kSha256),
      IsError());
}

TEST(ParseBuildStringTests, Sha256FragmentOnDirectoryFail) {
  EXPECT_THAT(
      ParseBuildString(std::string("gs://bucket/dist/#sha256=") + kSha256),
      IsError());
}

TEST(ParseBuildStringTests, CharactersAfterFilepathFail) {
  EXPECT_THAT(ParseBuildString("gs://bucket/{boot.img}trailing"), IsError());
  EXPECT_THAT(ParseBuildString("https://example.com/{boot.img}/more"),
              IsError());
}

TEST(ParseBuildStringTests, UrlWithCommaFail) {
  EXPECT_THAT(ParseBuildString("gs://bucket/file,name.zip"),
              IsErrorAndMessage(HasSubstr("comma-separated")));
}

TEST(ParseBuildStringTests, QueryStringOnObjectSuccess) {
  auto result = ParseBuildString("https://example.com/file.zip?sig=abc");
  EXPECT_THAT(result, IsOk());
  EXPECT_THAT(result.value(),
              VariantWith<HttpBuildString>(HttpBuildString{
                  .url = "https://example.com/file.zip?sig=abc"}));
}

TEST(ParseBuildStringTests, QueryStringOnDirectoryFail) {
  EXPECT_THAT(ParseBuildString("https://example.com/dist/?sig=abc"), IsError());
}

TEST(SingleBuildStringGflagsCompatFlagTests, GcsBuildStringSuccess) {
  std::optional<BuildString> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=gs://bucket/image.zip"}), IsOk());
  ASSERT_THAT(value, Optional(VariantWith<GcsBuildString>(
                         GcsBuildString{.url = "gs://bucket/image.zip"})));
}

TEST(SingleBuildStringGflagsCompatFlagTests, HttpBuildStringSuccess) {
  std::optional<BuildString> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=https://example.com/dist/"}),
              IsOk());
  ASSERT_THAT(value, Optional(VariantWith<HttpBuildString>(
                         HttpBuildString{.url = "https://example.com/dist/"})));
}

TEST(BuildStringGflagsCompatFlagTests, UrlMultiValueSuccess) {
  std::vector<std::optional<BuildString>> value;
  auto flag = GflagsCompatFlag("myflag", value);

  ASSERT_THAT(ConsumeFlags({flag}, {"--myflag=gs://bucket/a.zip,https://"
                                    "example.com/dist/"}),
              IsOk());
  ASSERT_THAT(value, SizeIs(2));
  ASSERT_THAT(value,
              ElementsAre(Optional(VariantWith<GcsBuildString>(
                              GcsBuildString{.url = "gs://bucket/a.zip"})),
                          Optional(VariantWith<HttpBuildString>(HttpBuildString{
                              .url = "https://example.com/dist/"}))));
}

}  // namespace cuttlefish

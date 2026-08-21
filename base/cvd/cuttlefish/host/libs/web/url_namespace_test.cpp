//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/url_namespace.h"

#include <optional>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;

TEST(UrlSchemeTests, SchemesSuccess) {
  EXPECT_EQ(UrlScheme("gs://bucket/object.zip"), "gs");
  EXPECT_EQ(UrlScheme("https://example.com/f.zip"), "https");
  EXPECT_EQ(UrlScheme("s3+v4.1://bucket/f.zip"), "s3+v4.1");
}

TEST(UrlSchemeTests, NonSchemesFail) {
  EXPECT_EQ(UrlScheme(""), std::nullopt);
  EXPECT_EQ(UrlScheme("://example.com/f.zip"), std::nullopt);
  EXPECT_EQ(UrlScheme("1s://example.com/f.zip"), std::nullopt);
  EXPECT_EQ(UrlScheme("branch/target"), std::nullopt);
  EXPECT_EQ(UrlScheme("gs:/bucket/object.zip"), std::nullopt);
}

TEST(ParseUrlTests, DecompositionSuccess) {
  struct TestCase {
    std::string_view url;
    std::string_view authority;
    std::string_view path;
    bool directory_form;
    std::string_view query;
  };
  const TestCase cases[] = {
      {"gs://bucket/dist/", "bucket", "dist/", true, ""},
      {"gs://bucket/", "bucket", "", true, ""},
      {"gs://bucket/a/b/c.zip", "bucket", "a/b/c.zip", false, ""},
      {"https://example.com/dist/", "example.com", "dist/", true, ""},
      {"https://example.com:8443/f.zip", "example.com:8443", "f.zip", false,
       ""},
      {"https://example.com/f.zip?sig=abc&x=1", "example.com", "f.zip", false,
       "sig=abc&x=1"},
  };
  for (const TestCase& test_case : cases) {
    Result<ParsedUrl> result = ParseUrl(test_case.url);
    ASSERT_THAT(result, IsOk()) << test_case.url;
    EXPECT_EQ(result->authority, test_case.authority) << test_case.url;
    EXPECT_EQ(result->path, test_case.path) << test_case.url;
    EXPECT_EQ(result->IsDirectoryForm(), test_case.directory_form)
        << test_case.url;
    EXPECT_EQ(result->query, test_case.query) << test_case.url;
  }
}

TEST(ParseUrlTests, MalformedFail) {
  EXPECT_THAT(ParseUrl("bucket/object.zip"), IsError());
  EXPECT_THAT(ParseUrl("gs://bucket"), IsError());
  EXPECT_THAT(ParseUrl("gs:///object.zip"), IsError());
}

TEST(ParseUrlTests, ErrorWithoutQueryFail) {
  EXPECT_THAT(ParseUrl("https://example.com?sig=secret"),
              IsErrorAndMessage(Not(HasSubstr("sig=secret"))));
}

TEST(DeriveProductTests, ImageZipNameSuccess) {
  EXPECT_EQ(DeriveProduct("aosp_cf_x86_64_phone-img-12345.zip"),
            "aosp_cf_x86_64_phone");
}

TEST(DeriveProductTests, ImageZipNameWithoutABuildIdSuccess) {
  EXPECT_EQ(DeriveProduct("aosp_cf_x86_64_auto-img.zip"),
            "aosp_cf_x86_64_auto");
}

TEST(DeriveProductTests, OtherNamesFail) {
  EXPECT_EQ(DeriveProduct("images.zip"), std::nullopt);
  EXPECT_EQ(DeriveProduct("bzImage"), std::nullopt);
  EXPECT_EQ(DeriveProduct("-img-12345.zip"), std::nullopt);
  EXPECT_EQ(DeriveProduct("-img.zip"), std::nullopt);
  EXPECT_EQ(DeriveProduct("phone-img-12345.tar.gz"), std::nullopt);
}

TEST(ResolveUrlZipNameTests, ObjectNamingTheKindSuccess) {
  EXPECT_THAT(ResolveUrlZipName("phone-img-1.zip", BuildZipKind::kImages),
              IsOkAndValue("phone-img-1.zip"));
  EXPECT_THAT(
      ResolveUrlZipName("phone-target_files-1.zip", BuildZipKind::kTargetFiles),
      IsOkAndValue("phone-target_files-1.zip"));
  EXPECT_THAT(ResolveUrlZipName("phone-img.zip", BuildZipKind::kImages),
              IsOkAndValue("phone-img.zip"));
}

TEST(ResolveUrlZipNameTests, ObjectIsTheImageZipSuccess) {
  EXPECT_THAT(ResolveUrlZipName("images.zip", BuildZipKind::kImages),
              IsOkAndValue("images.zip"));
}

TEST(ResolveUrlZipNameTests, ObjectForOtherKindFail) {
  EXPECT_THAT(ResolveUrlZipName("images.zip", BuildZipKind::kTargetFiles),
              IsErrorAndMessage(
                  AllOf(HasSubstr("images.zip"), HasSubstr("target_files"))));
}

TEST(ResolveUrlZipNameTests, ObjectIsNotAZipFail) {
  EXPECT_THAT(ResolveUrlZipName("bzImage", BuildZipKind::kImages),
              IsErrorAndMessage(HasSubstr("bzImage")));
}

TEST(ResolveUrlZipNameTests, ObjectNamingAnotherKindFail) {
  EXPECT_THAT(
      ResolveUrlZipName("phone-target_files-1.zip", BuildZipKind::kImages),
      IsError());
  EXPECT_THAT(ResolveUrlZipName("phone-otatools-1.zip", BuildZipKind::kImages),
              IsError());
  EXPECT_THAT(
      ResolveUrlZipName("phone-target_files.zip", BuildZipKind::kImages),
      IsError());
}

TEST(ResolveUrlZipNameTests, ListingWithOneMatchSuccess) {
  EXPECT_THAT(ResolveUrlZipName({"misc_info.txt", "phone-img-1.zip",
                                 "phone-target_files-1.zip"},
                                BuildZipKind::kImages),
              IsOkAndValue("phone-img-1.zip"));
}

TEST(ResolveUrlZipNameTests, ListingWithAZipWithoutABuildIdSuccess) {
  EXPECT_THAT(ResolveUrlZipName({"misc_info.txt", "aosp_cf_x86_64_auto-img.zip",
                                 "cvd-host_package.tar.gz"},
                                BuildZipKind::kImages),
              IsOkAndValue("aosp_cf_x86_64_auto-img.zip"));
}

TEST(ResolveUrlZipNameTests, ListingMatchesTheKindAskedForSuccess) {
  const std::vector<std::string> names = {"phone-img.zip",
                                          "phone-target_files.zip"};

  EXPECT_THAT(ResolveUrlZipName(names, BuildZipKind::kImages),
              IsOkAndValue("phone-img.zip"));
  EXPECT_THAT(ResolveUrlZipName(names, BuildZipKind::kTargetFiles),
              IsOkAndValue("phone-target_files.zip"));
}

TEST(ResolveUrlZipNameTests, ListingCountsEachNameOnceSuccess) {
  const std::vector<std::string> names = {"phone-img-1-img.zip"};

  EXPECT_THAT(ResolveUrlZipName(names, BuildZipKind::kImages),
              IsOkAndValue("phone-img-1-img.zip"));
}

TEST(ResolveUrlZipNameTests, ListingWithBothZipNamingsFail) {
  const std::vector<std::string> names = {"phone-img-1.zip", "phone-img.zip"};

  EXPECT_THAT(
      ResolveUrlZipName(names, BuildZipKind::kImages),
      IsErrorAndMessage(AllOf(HasSubstr("found 2"), HasSubstr("phone-img.zip"),
                              HasSubstr("phone-img-1.zip"))));
}

TEST(ResolveUrlZipNameTests, ListingWithoutAMatchFail) {
  const std::vector<std::string> names = {"misc_info.txt",
                                          "cvd-host_package.tar.gz"};

  EXPECT_THAT(
      ResolveUrlZipName(names, BuildZipKind::kImages),
      IsErrorAndMessage(AllOf(HasSubstr("img"), HasSubstr("misc_info.txt"))));
}

TEST(ResolveUrlZipNameTests, ListingWithSeveralMatchesFail) {
  const std::vector<std::string> names = {"phone-img-1.zip",
                                          "tablet-img-2.zip"};

  EXPECT_THAT(ResolveUrlZipName(names, BuildZipKind::kImages),
              IsErrorAndMessage(AllOf(HasSubstr("phone-img-1.zip"),
                                      HasSubstr("tablet-img-2.zip"))));
}

// The resolver takes no selector: a `{selector}` names the host package or an
// artifact to download, never the image zip.
TEST(ResolveUrlZipNameTests, SelectorCannotNameTheZipFail) {
  const std::vector<std::string> names = {"images.zip",
                                          "cvd-host_package.tar.gz"};

  EXPECT_THAT(ResolveUrlZipName(names, BuildZipKind::kImages), IsError());
}

}  // namespace
}  // namespace cuttlefish

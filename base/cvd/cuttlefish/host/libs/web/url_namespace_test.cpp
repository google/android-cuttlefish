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

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;

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
    EXPECT_EQ(result->directory_form, test_case.directory_form)
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

TEST(DeriveProductTests, OtherNamesSuccess) {
  EXPECT_EQ(DeriveProduct("images.zip"), "url");
  EXPECT_EQ(DeriveProduct("bzImage"), "url");
  EXPECT_EQ(DeriveProduct("-img-12345.zip"), "url");
  EXPECT_EQ(DeriveProduct("-img.zip"), "url");
  EXPECT_EQ(DeriveProduct("phone-img-12345.tar.gz"), "url");
}

TEST(ResolveUrlZipNameTests, ObjectNamingTheKindSuccess) {
  EXPECT_THAT(ResolveUrlZipName({}, true, "phone-img-1.zip", "img"),
              IsOkAndValue("phone-img-1.zip"));
  EXPECT_THAT(
      ResolveUrlZipName({}, true, "phone-target_files-1.zip", "target_files"),
      IsOkAndValue("phone-target_files-1.zip"));
  EXPECT_THAT(ResolveUrlZipName({}, true, "phone-img.zip", "img"),
              IsOkAndValue("phone-img.zip"));
}

TEST(ResolveUrlZipNameTests, ObjectIsTheImageZipSuccess) {
  EXPECT_THAT(ResolveUrlZipName({}, true, "images.zip", "img"),
              IsOkAndValue("images.zip"));
}

TEST(ResolveUrlZipNameTests, ObjectForOtherKindFail) {
  EXPECT_THAT(ResolveUrlZipName({}, true, "images.zip", "target_files"),
              IsErrorAndMessage(
                  AllOf(HasSubstr("images.zip"), HasSubstr("target_files"))));
}

TEST(ResolveUrlZipNameTests, ObjectIsNotAZipFail) {
  EXPECT_THAT(ResolveUrlZipName({}, true, "bzImage", "img"),
              IsErrorAndMessage(HasSubstr("bzImage")));
}

TEST(ResolveUrlZipNameTests, ObjectNamingAnotherKindFail) {
  EXPECT_THAT(ResolveUrlZipName({}, true, "phone-target_files-1.zip", "img"),
              IsError());
  EXPECT_THAT(ResolveUrlZipName({}, true, "phone-otatools-1.zip", "img"),
              IsError());
  EXPECT_THAT(ResolveUrlZipName({}, true, "phone-target_files.zip", "img"),
              IsError());
}

TEST(ResolveUrlZipNameTests, DirectoryWithOneMatchSuccess) {
  EXPECT_THAT(ResolveUrlZipName({"misc_info.txt", "phone-img-1.zip",
                                 "phone-target_files-1.zip"},
                                true, std::nullopt, "img"),
              IsOkAndValue("phone-img-1.zip"));
}

TEST(ResolveUrlZipNameTests, DirectoryWithAZipWithoutABuildIdSuccess) {
  EXPECT_THAT(ResolveUrlZipName({"misc_info.txt", "aosp_cf_x86_64_auto-img.zip",
                                 "cvd-host_package.tar.gz"},
                                true, std::nullopt, "img"),
              IsOkAndValue("aosp_cf_x86_64_auto-img.zip"));
}

TEST(ResolveUrlZipNameTests, DirectoryMatchesTheKindAskedForSuccess) {
  const std::vector<std::string> names = {"phone-img.zip",
                                          "phone-target_files.zip"};

  EXPECT_THAT(ResolveUrlZipName(names, true, std::nullopt, "img"),
              IsOkAndValue("phone-img.zip"));
  EXPECT_THAT(ResolveUrlZipName(names, true, std::nullopt, "target_files"),
              IsOkAndValue("phone-target_files.zip"));
}

TEST(ResolveUrlZipNameTests, DirectoryCountsEachNameOnceSuccess) {
  EXPECT_THAT(
      ResolveUrlZipName({"phone-img-1-img.zip"}, true, std::nullopt, "img"),
      IsOkAndValue("phone-img-1-img.zip"));
}

TEST(ResolveUrlZipNameTests, DirectoryWithBothZipNamingsFail) {
  EXPECT_THAT(
      ResolveUrlZipName({"phone-img-1.zip", "phone-img.zip"}, true,
                        std::nullopt, "img"),
      IsErrorAndMessage(AllOf(HasSubstr("found 2"), HasSubstr("phone-img.zip"),
                              HasSubstr("phone-img-1.zip"))));
}

TEST(ResolveUrlZipNameTests, DirectoryWithoutAMatchFail) {
  EXPECT_THAT(
      ResolveUrlZipName({"misc_info.txt", "cvd-host_package.tar.gz"}, true,
                        std::nullopt, "img"),
      IsErrorAndMessage(AllOf(HasSubstr("img"), HasSubstr("misc_info.txt"))));
}

TEST(ResolveUrlZipNameTests, DirectoryWithSeveralMatchesFail) {
  EXPECT_THAT(ResolveUrlZipName({"phone-img-1.zip", "tablet-img-2.zip"}, true,
                                std::nullopt, "img"),
              IsErrorAndMessage(AllOf(HasSubstr("phone-img-1.zip"),
                                      HasSubstr("tablet-img-2.zip"))));
}

TEST(ResolveUrlZipNameTests, OpenNamespaceFail) {
  EXPECT_THAT(ResolveUrlZipName({}, false, std::nullopt, "img"),
              IsErrorAndMessage(HasSubstr("gs://")));
}

// The resolver takes no selector: a `{selector}` names the host package or an
// artifact to download, never the image zip.
TEST(ResolveUrlZipNameTests, SelectorCannotNameTheZipFail) {
  EXPECT_THAT(ResolveUrlZipName({"images.zip", "cvd-host_package.tar.gz"}, true,
                                std::nullopt, "img"),
              IsError());
}

}  // namespace cuttlefish

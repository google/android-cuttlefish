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

#include "cuttlefish/host/libs/web/android_build.h"

#include <optional>
#include <sstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;

constexpr char kSignedUrl[] =
    "https://example.com/dist/phone-img-1.zip?X-Goog-Signature=secret";

std::string Print(const Build& build) {
  std::stringstream out;
  out << build;
  return out.str();
}

TEST(GcsBuildTests, DirectoryFormSuccess) {
  Result<GcsBuild> build =
      GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});

  ASSERT_THAT(build, IsOk());
  EXPECT_EQ(build->bucket, "bucket");
  EXPECT_EQ(build->prefix, "dist/");
  EXPECT_EQ(build->object, std::nullopt);
  EXPECT_EQ(build->id, "gs://bucket/dist/");
  EXPECT_EQ(build->target, "url");
  EXPECT_EQ(build->product, "url");
}

TEST(GcsBuildTests, ObjectFormSuccess) {
  Result<GcsBuild> build = GcsBuild::FromBuildString(GcsBuildString{
      .url = "gs://bucket/dist/phone-img-1.zip",
      .filepath = "boot.img",
      .sha256 = std::string(64, 'a'),
  });

  ASSERT_THAT(build, IsOk());
  EXPECT_EQ(build->bucket, "bucket");
  EXPECT_EQ(build->prefix, "dist/");
  EXPECT_EQ(build->object, "phone-img-1.zip");
  EXPECT_EQ(build->product, "phone");
  EXPECT_EQ(build->filepath, "boot.img");
  EXPECT_EQ(build->sha256, std::string(64, 'a'));
}

TEST(GcsBuildTests, BucketRootSuccess) {
  Result<GcsBuild> build =
      GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/"});

  ASSERT_THAT(build, IsOk());
  EXPECT_TRUE(build->prefix.empty());
  EXPECT_EQ(build->object, std::nullopt);
}

TEST(GcsBuildTests, MalformedUrlFail) {
  EXPECT_THAT(GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket"}),
              IsError());
}

TEST(HttpBuildTests, ObjectFormKeepsTheQueryOutOfTheIdSuccess) {
  Result<HttpBuild> build =
      HttpBuild::FromBuildString(HttpBuildString{.url = kSignedUrl});

  ASSERT_THAT(build, IsOk());
  EXPECT_EQ(build->url, kSignedUrl);
  EXPECT_TRUE(build->base.empty());
  EXPECT_EQ(build->object, "phone-img-1.zip");
  EXPECT_EQ(build->id, "https://example.com/dist/phone-img-1.zip");
  EXPECT_EQ(build->target, "url");
  EXPECT_EQ(build->product, "phone");
}

TEST(HttpBuildTests, DirectoryFormSuccess) {
  Result<HttpBuild> build = HttpBuild::FromBuildString(
      HttpBuildString{.url = "https://example.com/dist/"});

  ASSERT_THAT(build, IsOk());
  EXPECT_TRUE(build->url.empty());
  EXPECT_EQ(build->base, "https://example.com/dist/");
  EXPECT_EQ(build->object, std::nullopt);
  EXPECT_EQ(build->id, "https://example.com/dist/");
  EXPECT_EQ(build->product, "url");
}

TEST(BuildPrintingTests, UrlBuildsWithoutTheQuerySuccess) {
  Build gcs_build =
      *GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});
  Build http_build = *HttpBuild::FromBuildString(HttpBuildString{
      .url = kSignedUrl,
      .filepath = "boot.img",
  });

  EXPECT_THAT(Print(gcs_build), HasSubstr("gs://bucket/dist/"));
  EXPECT_THAT(Print(http_build),
              AllOf(HasSubstr("https://example.com/dist/phone-img-1.zip"),
                    HasSubstr("boot.img"), Not(HasSubstr("secret"))));
}

TEST(FetchLabelTests, UrlBuildsAreTheBareUrlSuccess) {
  Build gcs_build =
      *GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});
  Build http_build =
      *HttpBuild::FromBuildString(HttpBuildString{.url = kSignedUrl});

  EXPECT_EQ(FetchLabel(gcs_build), "gs://bucket/dist/");
  EXPECT_EQ(FetchLabel(http_build), "https://example.com/dist/phone-img-1.zip");
}

TEST(FetchLabelTests, OtherBuildsAreIdAndTargetSuccess) {
  Build device_build = DeviceBuild{.id = "123", .target = "test_target"};
  Build directory_build =
      DirectoryBuild({"/tmp/build"}, "test_target", std::nullopt);

  EXPECT_EQ(FetchLabel(device_build), "123/test_target");
  EXPECT_EQ(FetchLabel(directory_build), "eng/test_target");
}

}  // namespace
}  // namespace cuttlefish

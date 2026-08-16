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

#include <algorithm>
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

TEST(ArtifactSha256Tests, OnlyTheNamedObjectSuccess) {
  Build build = *GcsBuild::FromBuildString(GcsBuildString{
      .url = "gs://bucket/dist/phone-img-1.zip",
      .filepath = "boot.img",
      .sha256 = std::string(64, 'a'),
  });

  EXPECT_EQ(ArtifactSha256(build, "phone-img-1.zip"), std::string(64, 'a'));
  EXPECT_EQ(ArtifactSha256(build, "boot.img"), std::nullopt);
  EXPECT_EQ(ArtifactSha256(DeviceBuild{.id = "123"}, "img.zip"), std::nullopt);
}

TEST(BuildHasArtifactTests, ListedArtifactsSuccess) {
  GcsBuild build =
      *GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});
  build.contents = {{"a.txt", GcsObjectInfo{.generation = "1"}}};

  EXPECT_TRUE(BuildHasArtifact(build, "a.txt"));
  EXPECT_FALSE(BuildHasArtifact(build, "misc_info.txt"));
}

TEST(BuildHasArtifactTests, UnlistedNamespacesAnswerYesSuccess) {
  Build http_directory = *HttpBuild::FromBuildString(
      HttpBuildString{.url = "https://example.com/dist/"});
  Build gcs_object = *GcsBuild::FromBuildString(
      GcsBuildString{.url = "gs://bucket/dist/phone-img-1.zip"});

  EXPECT_TRUE(BuildHasArtifact(http_directory, "misc_info.txt"));
  EXPECT_TRUE(BuildHasArtifact(gcs_object, "phone-img-1.zip"));
  EXPECT_TRUE(BuildHasArtifact(DeviceBuild{.id = "123"}, "misc_info.txt"));
}

TEST(BuildCacheKeyTests, AndroidBuildsAreTheIdAndTargetSuccess) {
  Build device_build = DeviceBuild{.id = "123", .target = "test_target"};
  Build directory_build =
      DirectoryBuild({"/tmp/build"}, "test_target", std::nullopt);

  EXPECT_EQ(BuildCacheKey(device_build, "img.zip"), "123/test_target");
  EXPECT_EQ(BuildCacheKey(directory_build, "img.zip"), "eng/test_target");
}

TEST(BuildCacheKeyTests, GcsObjectGenerationsDifferSuccess) {
  GcsBuild build = *GcsBuild::FromBuildString(
      GcsBuildString{.url = "gs://bucket/dist/phone-img-1.zip"});
  build.generation = "17";
  std::string first = BuildCacheKey(build, "phone-img-1.zip");
  build.generation = "18";
  std::string second = BuildCacheKey(build, "phone-img-1.zip");

  EXPECT_THAT(first, ::testing::EndsWith("/17"));
  EXPECT_THAT(second, ::testing::EndsWith("/18"));
  EXPECT_THAT(first, ::testing::StartsWith("url/"));
  // The URL is what the two keys share.
  EXPECT_EQ(first.substr(0, first.rfind('/')),
            second.substr(0, second.rfind('/')));
}

TEST(BuildCacheKeyTests, GcsDirectoryArtifactsAreKeyedApartSuccess) {
  GcsBuild build =
      *GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});
  build.contents = {
      {"a.txt", GcsObjectInfo{.generation = "1"}},
      {"b.txt", GcsObjectInfo{.generation = "2"}},
  };

  EXPECT_THAT(BuildCacheKey(build, "a.txt"), ::testing::EndsWith("/1"));
  EXPECT_THAT(BuildCacheKey(build, "b.txt"), ::testing::EndsWith("/2"));
  EXPECT_EQ(BuildCacheKey(build, "absent.txt"), "");
}

TEST(BuildCacheKeyTests, HttpObjectsUseTheEtagSuccess) {
  HttpBuild build =
      *HttpBuild::FromBuildString(HttpBuildString{.url = kSignedUrl});
  build.etag = "W/\"a/b\"";

  std::string key = BuildCacheKey(build, "phone-img-1.zip");
  EXPECT_THAT(key, ::testing::StartsWith("url/"));
  EXPECT_THAT(key, Not(HasSubstr("\"")));
  EXPECT_THAT(key, Not(HasSubstr("secret")));
  EXPECT_EQ(std::count(key.begin(), key.end(), '/'), 2);
}

TEST(BuildCacheKeyTests, HttpObjectsFallBackToTheDigestSuccess) {
  HttpBuild build = *HttpBuild::FromBuildString(HttpBuildString{
      .url = "https://example.com/dist/phone-img-1.zip",
      .sha256 = std::string(64, 'a'),
  });

  EXPECT_THAT(BuildCacheKey(build, "phone-img-1.zip"),
              ::testing::EndsWith(std::string(64, 'a')));
}

TEST(BuildCacheKeyTests, HttpDirectoriesHaveNoKeyFail) {
  HttpBuild build = *HttpBuild::FromBuildString(
      HttpBuildString{.url = "https://example.com/dist/"});

  EXPECT_EQ(BuildCacheKey(build, "misc_info.txt"), "");
}

}  // namespace
}  // namespace cuttlefish

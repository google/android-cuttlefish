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

#include "cuttlefish/host/libs/web/caching_build_api.h"

#include <string>
#include <vector>

#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::SizeIs;

constexpr char kContents[] = "artifact bytes";
constexpr char kContentsSha256[] =
    "4659fc0570122b0e0aa14f4ff7c261b1fe51795a01ba79963f462ebf40d7520d";

// Records where the caching layer asked for each download and writes the bytes
// a real API would have written.
class RecordingBuildApi : public BuildApi {
 public:
  Result<Build> GetBuild(const BuildString&) override {
    return CF_ERR("not used");
  }

  Result<std::string> DownloadFile(const Build&,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override {
    directories.push_back(target_directory);
    CF_EXPECT(EnsureDirectoryExists(target_directory));
    std::string path = target_directory + "/" + artifact_name;
    CF_EXPECT(android::base::WriteStringToFile(kContents, path));
    return path;
  }

  Result<SeekableZipSource> FileReader(const Build&,
                                       const std::string&) override {
    return CF_EXPECT(
        WritableZipSource::BorrowData(contents.data(), contents.size()));
  }

  std::vector<std::string> directories;
  std::string contents = kContents;
};

class CachingBuildApiTests : public ::testing::Test {
 protected:
  std::string CachePath(const std::string& relative) const {
    return std::string(cache_.path) + "/" + relative;
  }

  RecordingBuildApi inner_;
  TemporaryDir cache_;
  TemporaryDir target_;
  CachingBuildApi api_{inner_, std::string(cache_.path)};
};

TEST_F(CachingBuildApiTests, DownloadFileKeysOnTheBuildIdAndTargetSuccess) {
  Build build = DeviceBuild{.id = "123", .target = "test_target"};

  EXPECT_THAT(api_.DownloadFile(build, target_.path, "img.zip"),
              IsOkAndValue(std::string(target_.path) + "/img.zip"));
  EXPECT_THAT(inner_.directories, ElementsAre(CachePath("123/test_target")));
}

TEST_F(CachingBuildApiTests, DownloadFileServesASecondCallFromTheCacheSuccess) {
  Build build = DeviceBuild{.id = "123", .target = "test_target"};

  ASSERT_THAT(api_.DownloadFile(build, target_.path, "img.zip"), IsOk());
  ASSERT_THAT(api_.DownloadFile(build, target_.path, "img.zip"), IsOk());
  EXPECT_THAT(inner_.directories, SizeIs(1));
}

TEST_F(CachingBuildApiTests, FileReaderKeysOnTheBuildSuccess) {
  Build build = DeviceBuild{.id = "123", .target = "test_target"};

  EXPECT_THAT(api_.FileReader(build, "img.zip"), IsOk());
  EXPECT_TRUE(FileExists(CachePath("123/test_target/img.zip")));
}

TEST_F(CachingBuildApiTests, DownloadFileKeysOnTheObjectGenerationSuccess) {
  GcsBuild build = *GcsBuild::FromBuildString(
      GcsBuildString{.url = "gs://bucket/dist/phone-img-1.zip"});
  build.generation = "17";
  ASSERT_THAT(api_.DownloadFile(build, target_.path, "phone-img-1.zip"),
              IsOk());
  build.generation = "18";
  ASSERT_THAT(api_.DownloadFile(build, target_.path, "phone-img-1.zip"),
              IsOk());

  EXPECT_THAT(inner_.directories, SizeIs(2));
  EXPECT_NE(inner_.directories[0], inner_.directories[1]);
  EXPECT_THAT(inner_.directories[0], HasSubstr("/url/"));
}

TEST_F(CachingBuildApiTests, DownloadFileKeysEachListedObjectSuccess) {
  GcsBuild build =
      *GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});
  build.contents = {
      {"a.txt", GcsObjectInfo{.generation = "1"}},
      {"b.txt", GcsObjectInfo{.generation = "2"}},
  };

  ASSERT_THAT(api_.DownloadFile(build, target_.path, "a.txt"), IsOk());
  ASSERT_THAT(api_.DownloadFile(build, target_.path, "b.txt"), IsOk());

  EXPECT_THAT(inner_.directories, SizeIs(2));
  EXPECT_NE(inner_.directories[0], inner_.directories[1]);
}

TEST_F(CachingBuildApiTests, DownloadFileOfAnUnversionedArtifactSkipsTheCache) {
  Build build = *HttpBuild::FromBuildString(
      HttpBuildString{.url = "https://example.com/dist/"});

  EXPECT_THAT(api_.DownloadFile(build, target_.path, "misc_info.txt"),
              IsOkAndValue(std::string(target_.path) + "/misc_info.txt"));
  EXPECT_THAT(inner_.directories, ElementsAre(std::string(target_.path)));
  EXPECT_THAT(IsDirectoryEmpty(cache_.path), IsOkAndValue(true));
}

// The listing answers that the build does not hold the artifact, so the
// caching layer has nothing to say about it and the download reports it.
TEST_F(CachingBuildApiTests, DownloadFileOfAnUnlistedArtifactSkipsTheCache) {
  GcsBuild build =
      *GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});
  build.contents = {{"a.txt", GcsObjectInfo{.generation = "1"}}};

  EXPECT_THAT(api_.DownloadFile(build, target_.path, "misc_info.txt"),
              IsOkAndValue(std::string(target_.path) + "/misc_info.txt"));
  EXPECT_THAT(inner_.directories, ElementsAre(std::string(target_.path)));
  EXPECT_THAT(IsDirectoryEmpty(cache_.path), IsOkAndValue(true));
}

TEST_F(CachingBuildApiTests, DownloadFileChecksTheCachedDigestFail) {
  GcsBuild build = *GcsBuild::FromBuildString(GcsBuildString{
      .url = "gs://bucket/dist/phone-img-1.zip",
      .sha256 = std::string(64, 'a'),
  });
  build.generation = "17";

  ASSERT_THAT(api_.DownloadFile(build, target_.path, "phone-img-1.zip"),
              IsOk());
  EXPECT_THAT(api_.DownloadFile(build, target_.path, "phone-img-1.zip"),
              IsErrorAndMessage(HasSubstr(kContentsSha256)));
}

}  // namespace
}  // namespace cuttlefish

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

#include "cuttlefish/host/libs/web/composite_build_api.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_api.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/android_build_url.h"
#include "cuttlefish/host/libs/web/gcs_build_api.h"
#include "cuttlefish/host/libs/web/http_build_api.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::VariantWith;

constexpr char kAndroidBuildHost[] = "androidbuild-pa.googleapis.com";
constexpr char kStorageHost[] = "storage.googleapis.com";
constexpr char kObjectUrl[] = "https://example.com/dist/img.zip";
constexpr char kDirectoryUrl[] = "https://example.com/dist/";

// Every request reaches the same client, so the host each build string or
// build is sent to is what the routing assertions read. Nothing is set up to
// answer, so each routed call fails at the API it reached.
class CompositeBuildApiTests : public ::testing::Test {
 protected:
  GcsBuild ListedGcsBuild() {
    GcsBuild build =
        *GcsBuild::FromBuildString(GcsBuildString{.url = "gs://bucket/dist/"});
    build.contents = {{"a.txt", GcsObjectInfo{.generation = "1"}}};
    return build;
  }

  DirectoryBuild LocalBuild() {
    return DirectoryBuild(std::vector<std::string>{directory_.path}, "target",
                          std::nullopt);
  }

  FakeHttpClient http_client_;
  AndroidBuildUrl android_build_url_{
      "https://androidbuild-pa.googleapis.com/v4", "", ""};
  CompositeBuildApi api_{
      std::make_unique<AndroidBuildApi>(http_client_, android_build_url_),
      std::make_unique<GcsBuildApi>(http_client_, nullptr),
      std::make_unique<HttpBuildApi>(http_client_)};
  TemporaryDir directory_;
};

TEST_F(CompositeBuildApiTests, GetBuildRoutesByStringAlternativeSuccess) {
  Result<Build> device = api_.GetBuild(
      DeviceBuildString{.branch_or_id = "aosp-main", .target = "target"});
  EXPECT_THAT(device, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kAndroidBuildHost));

  EXPECT_THAT(api_.GetBuild(DirectoryBuildString{
                  .paths = std::vector<std::string>{directory_.path},
                  .target = "target"}),
              IsOkAndValue(VariantWith<DirectoryBuild>(_)));

  Result<Build> gcs = api_.GetBuild(GcsBuildString{.url = "gs://bucket/dist/"});
  EXPECT_THAT(gcs, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kStorageHost));

  Result<Build> http = api_.GetBuild(HttpBuildString{.url = kObjectUrl});
  EXPECT_THAT(http, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kObjectUrl));
}

TEST_F(CompositeBuildApiTests, DownloadFileRoutesByBuildAlternativeSuccess) {
  Result<std::string> device = api_.DownloadFile(
      DeviceBuild{.id = "1", .target = "target"}, directory_.path, "a.txt");
  EXPECT_THAT(device, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kAndroidBuildHost));

  EXPECT_THAT(api_.DownloadFile(LocalBuild(), directory_.path, "a.txt"),
              IsErrorAndMessage(HasSubstr("a.txt")));

  Result<std::string> gcs =
      api_.DownloadFile(ListedGcsBuild(), directory_.path, "a.txt");
  EXPECT_THAT(gcs, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kStorageHost));

  Result<std::string> http = api_.DownloadFile(
      *HttpBuild::FromBuildString(HttpBuildString{.url = kDirectoryUrl}),
      directory_.path, "a.txt");
  EXPECT_THAT(http, IsError());
  EXPECT_TRUE(http_client_.RequestMade("https://example.com/dist/a.txt"));
}

TEST_F(CompositeBuildApiTests, FileReaderRoutesByBuildAlternativeSuccess) {
  Result<SeekableZipSource> device =
      api_.FileReader(DeviceBuild{.id = "1", .target = "target"}, "a.zip");
  EXPECT_THAT(device, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kAndroidBuildHost));

  EXPECT_THAT(api_.FileReader(LocalBuild(), "a.zip"),
              IsErrorAndMessage(HasSubstr("a.zip")));

  Result<SeekableZipSource> gcs = api_.FileReader(ListedGcsBuild(), "a.txt");
  EXPECT_THAT(gcs, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kStorageHost));

  Result<SeekableZipSource> http = api_.FileReader(
      *HttpBuild::FromBuildString(HttpBuildString{.url = kObjectUrl}),
      "img.zip");
  EXPECT_THAT(http, IsError());
  EXPECT_TRUE(http_client_.RequestMade(kObjectUrl));
}

}  // namespace
}  // namespace cuttlefish

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

#include "cuttlefish/host/commands/cvd/fetch/fetch_context.h"

#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/fetch/builds.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_tracer.h"
#include "cuttlefish/host/commands/cvd/fetch/target_directories.h"
#include "cuttlefish/host/libs/config/fetcher_config.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/web/url_namespace.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::HasSubstr;

class MockBuildApi : public BuildApi {
 public:
  MOCK_METHOD(Result<Build>, GetBuild, (const BuildString&), (override));
  MOCK_METHOD(Result<std::string>, DownloadFile,
              (const Build&, const std::string&, const std::string&),
              (override));
  MOCK_METHOD(Result<SeekableZipSource>, FileReader,
              (const Build&, const std::string&), (override));
};

TEST(FetchContextTest, GetBuildZipNameGcsObject) {
  MockBuildApi mock_api;
  TargetDirectories target_directories;
  Builds builds = {.default_build = GcsBuild{.bucket = "bucket",
                                             .object = "phone-img-1.zip"}};
  FetcherConfig fetcher_config;
  FetchTracer tracer;
  FetchContext fetch_context(mock_api, target_directories, builds,
                             fetcher_config, tracer);

  std::optional<FetchBuildContext> context = fetch_context.DefaultBuild();

  ASSERT_TRUE(context.has_value());
  ASSERT_THAT(context->GetBuildZipName(BuildZipKind::kImages),
              IsOkAndValue("phone-img-1.zip"));
}

TEST(FetchContextTest, GetBuildZipNameGcsListing) {
  MockBuildApi mock_api;
  TargetDirectories target_directories;
  Builds builds = {
      .default_build = GcsBuild{
          .bucket = "bucket",
          .contents = {{"misc_info.txt", {}}, {"phone-img-1.zip", {}}}}};
  FetcherConfig fetcher_config;
  FetchTracer tracer;
  FetchContext fetch_context(mock_api, target_directories, builds,
                             fetcher_config, tracer);

  std::optional<FetchBuildContext> context = fetch_context.DefaultBuild();

  ASSERT_TRUE(context.has_value());
  ASSERT_THAT(context->GetBuildZipName(BuildZipKind::kImages),
              IsOkAndValue("phone-img-1.zip"));
}

TEST(FetchContextTest, GetBuildZipNameHttpObject) {
  MockBuildApi mock_api;
  TargetDirectories target_directories;
  Builds builds = {.default_build = HttpBuild{
                       .url = "https://example.com/dist/phone-img-1.zip",
                       .object = "phone-img-1.zip"}};
  FetcherConfig fetcher_config;
  FetchTracer tracer;
  FetchContext fetch_context(mock_api, target_directories, builds,
                             fetcher_config, tracer);

  std::optional<FetchBuildContext> context = fetch_context.DefaultBuild();

  ASSERT_TRUE(context.has_value());
  ASSERT_THAT(context->GetBuildZipName(BuildZipKind::kImages),
              IsOkAndValue("phone-img-1.zip"));
}

TEST(FetchContextTest, GetBuildZipNameHttpDirectoryNoListing) {
  MockBuildApi mock_api;
  TargetDirectories target_directories;
  Builds builds = {.default_build =
                       HttpBuild{.url = "https://example.com/dist/"}};
  FetcherConfig fetcher_config;
  FetchTracer tracer;
  FetchContext fetch_context(mock_api, target_directories, builds,
                             fetcher_config, tracer);

  std::optional<FetchBuildContext> context = fetch_context.DefaultBuild();

  ASSERT_TRUE(context.has_value());
  ASSERT_THAT(context->GetBuildZipName(BuildZipKind::kImages),
              IsErrorAndMessage(HasSubstr("gs://")));
}

TEST(FetchContextTest, GetBuildZipNameAndroidBuild) {
  MockBuildApi mock_api;
  TargetDirectories target_directories;
  Builds builds = {.default_build = DeviceBuild{
                       .id = "12345", .target = "test", .product = "phone"}};
  FetcherConfig fetcher_config;
  FetchTracer tracer;
  FetchContext fetch_context(mock_api, target_directories, builds,
                             fetcher_config, tracer);

  std::optional<FetchBuildContext> context = fetch_context.DefaultBuild();

  ASSERT_TRUE(context.has_value());
  ASSERT_THAT(context->GetBuildZipName(BuildZipKind::kTargetFiles),
              IsOkAndValue("phone-target_files-12345.zip"));
}

}  // namespace
}  // namespace cuttlefish

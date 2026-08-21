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

#include "cuttlefish/host/libs/web/http_build_api.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/zip_over_ranges.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Not;

constexpr char kObjectUrl[] = "https://example.com/dist/phone-img-1.zip";
constexpr char kSignedUrl[] =
    "https://example.com/dist/phone-img-1.zip?X-Goog-Signature=secret";

TEST(HttpBuildApiTests, GetBuildProbesWithARangedGetSuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  std::vector<std::string> seen_headers;
  http_client.SetResponse(
      [&seen_headers](const HttpRequest& request) {
        seen_headers = request.headers;
        return HttpResponse<std::string>{
            .data = "a",
            .http_code = 206,
            .headers = {{"etag", "\"v1\""}, {"accept-ranges", "bytes"}},
        };
      },
      kObjectUrl);

  HttpBuildString build_string = {.url = kSignedUrl};
  Result<HttpBuild> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  EXPECT_EQ(build->url, kSignedUrl);
  EXPECT_EQ(build->etag, "\"v1\"");
  EXPECT_TRUE(build->accept_ranges);
  // A pre-signed URL signs the verb, so the probe is a ranged GET.
  EXPECT_THAT(seen_headers, Contains("Range: bytes=0-0"));
  EXPECT_FALSE(HasAuthorization(seen_headers));
}

TEST(HttpBuildApiTests, GetBuildDoesNotProbeADirectorySuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  HttpBuildString build_string = {.url = "https://example.com/dist/"};
  Result<HttpBuild> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  EXPECT_FALSE(http_client.RequestMade("https://example.com/dist/"));
}

TEST(HttpBuildApiTests, GetBuildMissingUrlFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);
  http_client.SetResponse(HttpResponse<std::string>{.http_code = 404},
                          kObjectUrl);

  HttpBuildString build_string = {.url = kSignedUrl};
  EXPECT_THAT(api.GetBuild(build_string),
              IsErrorAndMessage(AllOf(HasSubstr(kObjectUrl), HasSubstr("404"),
                                      Not(HasSubstr("secret")))));
}

TEST(HttpBuildApiTests, GetBuildRedirectFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);
  http_client.SetResponse(HttpResponse<std::string>{.http_code = 302},
                          kObjectUrl);

  HttpBuildString build_string = {.url = kSignedUrl};
  EXPECT_THAT(api.GetBuild(build_string),
              IsErrorAndMessage(AllOf(HasSubstr(kObjectUrl), HasSubstr("302"),
                                      HasSubstr("redirect"))));
}

TEST(HttpBuildApiTests, DownloadFileFromADirectorySuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  bool authorized = false;
  http_client.SetResponse(
      [&authorized](const HttpRequest& request) {
        authorized = HasAuthorization(request.headers);
        return HttpResponse<std::string>{.data = "recovery_api_version=3",
                                         .http_code = 200};
      },
      "https://example.com/dist/misc_info.txt");

  HttpBuildString build_string = {.url = "https://example.com/dist/"};
  Result<HttpBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  std::string expected_path =
      std::string(target_directory.path) + "/misc_info.txt";
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path, "misc_info.txt"),
              IsOkAndValue(expected_path));

  EXPECT_THAT(ReadFileContents(expected_path),
              IsOkAndValue("recovery_api_version=3"));
  EXPECT_FALSE(authorized);
}

TEST(HttpBuildApiTests, DownloadFileAbsentFromADirectoryFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  HttpBuildString build_string = {.url = "https://example.com/dist/"};
  Result<HttpBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path, "absent.txt"),
              IsErrorAndMessage(AllOf(HasSubstr("absent.txt"),
                                      HasSubstr("https://example.com/dist/"),
                                      HasSubstr("404"))));
}

TEST(HttpBuildApiTests, DownloadFileAbsentFromAnObjectFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);
  http_client.SetResponse(
      HttpResponse<std::string>{.data = "a", .http_code = 206}, kObjectUrl);

  HttpBuildString build_string = {.url = kSignedUrl};
  Result<HttpBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path, "absent.txt"),
              IsErrorAndMessage(AllOf(HasSubstr("absent.txt"),
                                      HasSubstr("phone-img-1.zip"))));
}

TEST(HttpBuildApiTests, FileReaderReadsTheObjectSuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  Result<ZipOverRanges> zip_handler =
      ZipOverRanges::Create({{"boot.img", "boot bytes"}},
                            /*serve_ranges=*/true);
  ASSERT_THAT(zip_handler, IsOk());

  bool authorized = false;
  http_client.SetResponse(
      [&zip_handler, &authorized](const HttpRequest& request) {
        authorized |= HasAuthorization(request.headers);
        return (*zip_handler)(request);
      },
      kObjectUrl);

  HttpBuildString build_string = {.url = kSignedUrl};
  Result<HttpBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  Result<SeekableZipSource> source = api.FileReader(*build, "phone-img-1.zip");
  ASSERT_THAT(source, IsOk());
  Result<ReadableZip> zip = ReadableZip::FromSource(std::move(*source));
  ASSERT_THAT(zip, IsOk());
  Result<std::unique_ptr<ReaderSeeker>> member = zip->OpenReadOnly("boot.img");
  ASSERT_THAT(member, IsOk());
  EXPECT_THAT(ReadToString(**member), IsOkAndValue("boot bytes"));

  EXPECT_TRUE(http_client.RequestMade(kSignedUrl));
  EXPECT_FALSE(authorized);
}

TEST(HttpBuildApiTests, DownloadFileExtractsAnArchiveMemberSuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  Result<ZipOverRanges> zip_handler = ZipOverRanges::Create(
      {{"cvd-host_package.tar.gz", "package bytes"}, {"boot.img", "boot"}},
      /*serve_ranges=*/true);
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kObjectUrl);

  HttpBuildString build_string = {
      .url = kSignedUrl,
      .filepath = "cvd-host_package.tar.gz",
  };
  Result<HttpBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  std::string expected_path =
      std::string(target_directory.path) + "/cvd-host_package.tar.gz";
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path,
                               "cvd-host_package.tar.gz"),
              IsOkAndValue(expected_path));

  EXPECT_THAT(ReadFileContents(expected_path), IsOkAndValue("package bytes"));
  EXPECT_TRUE(zip_handler->RangeRequestMade());
}

TEST(HttpBuildApiTests, DownloadFileMemberWithoutRangeSupportFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  Result<ZipOverRanges> zip_handler = ZipOverRanges::Create(
      {{"cvd-host_package.tar.gz", "package bytes"}}, /*serve_ranges=*/false);
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kObjectUrl);

  HttpBuildString build_string = {
      .url = kSignedUrl,
      .filepath = "cvd-host_package.tar.gz",
  };
  Result<HttpBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());
  EXPECT_FALSE(build->accept_ranges);

  TemporaryDir target_directory;
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path,
                               "cvd-host_package.tar.gz"),
              IsErrorAndMessage(AllOf(HasSubstr("range requests"),
                                      HasSubstr("cvd-host_package.tar.gz"))));
}

}  // namespace
}  // namespace cuttlefish

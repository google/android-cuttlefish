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

#include "cuttlefish/host/libs/web/gcs_build_api.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/credential_source.h"
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
using ::testing::HasSubstr;
using ::testing::SizeIs;

constexpr char kObjectUrl[] =
    "https://storage.googleapis.com/storage/v1/b/bucket/o/"
    "dist%2Fphone-img-1.zip";
constexpr char kMediaUrl[] =
    "https://storage.googleapis.com/storage/v1/b/bucket/o/"
    "dist%2Fphone-img-1.zip?alt=media";
constexpr char kListUrl[] =
    "https://storage.googleapis.com/storage/v1/b/bucket/o?delimiter=%2F&"
    "prefix=dist%2F";

TEST(GcsBuildApiTests, GetBuildProbesObjectMetadataSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"size": "3", "generation": "17", "md5Hash": "m"})", kObjectUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/phone-img-1.zip"};
  Result<GcsBuild> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  EXPECT_EQ(build->object, "phone-img-1.zip");
  EXPECT_EQ(build->generation, "17");
  EXPECT_EQ(build->md5, "m");
  EXPECT_TRUE(http_client.RequestMade(kObjectUrl));
  EXPECT_FALSE(http_client.RequestMade("alt=media"));
}

TEST(GcsBuildApiTests, GetBuildAuthorizesWithACredentialSuccess) {
  FakeHttpClient http_client;
  std::unique_ptr<CredentialSource> credentials =
      FixedCredentialSource::Make("test-token");
  GcsBuildApi api(http_client, credentials.get());

  bool authorized = false;
  http_client.SetResponse(
      [&authorized](const HttpRequest& request) {
        authorized = HasAuthorization(request.headers);
        return HttpResponse<std::string>{.data = "{}", .http_code = 200};
      },
      kObjectUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/phone-img-1.zip"};
  ASSERT_THAT(api.GetBuild(build_string), IsOk());
  EXPECT_TRUE(authorized);
}

TEST(GcsBuildApiTests, GetBuildIsAnonymousWithoutACredentialSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);

  bool authorized = false;
  http_client.SetResponse(
      [&authorized](const HttpRequest& request) {
        authorized = HasAuthorization(request.headers);
        return HttpResponse<std::string>{.data = "{}", .http_code = 200};
      },
      kObjectUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/phone-img-1.zip"};
  ASSERT_THAT(api.GetBuild(build_string), IsOk());
  EXPECT_FALSE(authorized);
}

TEST(GcsBuildApiTests, GetBuildMissingObjectFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      HttpResponse<std::string>{.data = "{}", .http_code = 404}, kObjectUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/phone-img-1.zip"};
  EXPECT_THAT(
      api.GetBuild(build_string),
      IsErrorAndMessage(AllOf(HasSubstr("gs://bucket/dist/phone-img-1.zip"),
                              HasSubstr("404"), HasSubstr("Check log file"))));
}

TEST(GcsBuildApiTests, GetBuildAnonymousForbiddenNamesTheLoginCommandFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      HttpResponse<std::string>{.data = "{}", .http_code = 403}, kObjectUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/phone-img-1.zip"};
  EXPECT_THAT(
      api.GetBuild(build_string),
      IsErrorAndMessage(HasSubstr("cvd login "
                                  "--scopes=https://www.googleapis.com/auth/"
                                  "devstorage.read_only")));
}

TEST(GcsBuildApiTests, GetBuildEmptyPrefixFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(R"({"kind": "storage#objects"})", kListUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/"};
  EXPECT_THAT(api.GetBuild(build_string),
              IsErrorAndMessage(HasSubstr("gs://bucket/dist/")));
}

TEST(GcsBuildApiTests, GetBuildListingFollowsPageTokensSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      [](const HttpRequest& request) {
        std::string data =
            R"({"items": [{"name": "dist/a.txt", "generation": "1",
                           "md5Hash": "ma"}], "nextPageToken": "page2"})";
        if (absl::StrContains(request.url, "pageToken=page2")) {
          data =
              R"({"items": [{"name": "dist/b.txt", "generation": "2",
                             "md5Hash": "mb", "size": "12"},
                            {"name": "dist/"}]})";
        }
        return HttpResponse<std::string>{.data = data, .http_code = 200};
      },
      kListUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/"};
  Result<GcsBuild> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  EXPECT_THAT(build->contents, SizeIs(2));
  EXPECT_EQ(build->contents.at("a.txt").generation, "1");
  EXPECT_EQ(build->contents.at("b.txt").md5, "mb");
  EXPECT_EQ(build->contents.at("b.txt").size, 12);
  EXPECT_TRUE(http_client.RequestMade("pageToken=page2"));
}

TEST(GcsBuildApiTests, DownloadFileAbsentArtifactFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"items": [{"name": "dist/a.txt", "generation": "1"}]})", kListUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/"};
  Result<GcsBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path, "b.txt"),
              IsErrorAndMessage(AllOf(HasSubstr("b.txt"), HasSubstr("a.txt"),
                                      HasSubstr("gs://bucket/dist/"))));
  EXPECT_FALSE(http_client.RequestMade("alt=media"));
}

TEST(GcsBuildApiTests, DownloadFileWritesTheArtifactSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"items": [{"name": "dist/misc_info.txt", "generation": "1"}]})",
      kListUrl);
  http_client.SetResponse("recovery_api_version=3",
                          "b/bucket/o/dist%2Fmisc_info.txt?alt=media");

  GcsBuildString build_string = {.url = "gs://bucket/dist/"};
  Result<GcsBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  std::string expected_path =
      std::string(target_directory.path) + "/misc_info.txt";
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path, "misc_info.txt"),
              IsOkAndValue(expected_path));

  EXPECT_THAT(ReadFileContents(expected_path),
              IsOkAndValue("recovery_api_version=3"));
}

TEST(GcsBuildApiTests, DownloadFileChecksTheListedMd5Fail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"items": [{"name": "dist/misc_info.txt", "generation": "1",
                     "md5Hash": "AAAAAAAAAAAAAAAAAAAAAA=="}]})",
      kListUrl);
  http_client.SetResponse("recovery_api_version=3",
                          "b/bucket/o/dist%2Fmisc_info.txt?alt=media");

  GcsBuildString build_string = {.url = "gs://bucket/dist/"};
  Result<GcsBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path, "misc_info.txt"),
              IsErrorAndMessage(AllOf(HasSubstr("misc_info.txt"),
                                      HasSubstr("iJ/GrhggATmj4tovMOdNDQ=="),
                                      HasSubstr("AAAAAAAAAAAAAAAAAAAAAA=="))));
}

TEST(GcsBuildApiTests, DownloadFileChecksTheProbedMd5Success) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"size": "22", "generation": "17",
          "md5Hash": "iJ/GrhggATmj4tovMOdNDQ=="})",
      kObjectUrl);
  http_client.SetResponse("recovery_api_version=3", kMediaUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/phone-img-1.zip"};
  Result<GcsBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  EXPECT_THAT(
      api.DownloadFile(*build, target_directory.path, "phone-img-1.zip"),
      IsOk());
}

TEST(GcsBuildApiTests, DownloadFileChecksTheRequestedSha256Fail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(R"({"size": "22"})", kObjectUrl);
  http_client.SetResponse("recovery_api_version=3", kMediaUrl);

  GcsBuildString build_string = {
      .url = "gs://bucket/dist/phone-img-1.zip",
      .sha256 = std::string(64, 'b'),
  };
  Result<GcsBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  EXPECT_THAT(
      api.DownloadFile(*build, target_directory.path, "phone-img-1.zip"),
      IsErrorAndMessage(AllOf(
          HasSubstr("phone-img-1.zip"), HasSubstr(std::string(64, 'b')),
          HasSubstr("8e441a1db0c390234afe2970a82f888e5608304062d77df18622d872e"
                    "1328f5d"))));
}

TEST(GcsBuildApiTests, FileReaderReadsTheNamedArtifactSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);

  Result<ZipOverRanges> zip_handler =
      ZipOverRanges::Create({{"boot.img", "boot bytes"}});
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kMediaUrl);
  http_client.SetResponse(
      absl::StrCat(R"({"items": [{"name": "dist/phone-img-1.zip",)",
                   R"( "generation": "1", "size": ")", zip_handler->Size(),
                   R"("}]})"),
      kListUrl);

  GcsBuildString build_string = {.url = "gs://bucket/dist/"};
  Result<GcsBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  Result<SeekableZipSource> source = api.FileReader(*build, "phone-img-1.zip");
  ASSERT_THAT(source, IsOk());
  Result<ReadableZip> zip = ReadableZip::FromSource(std::move(*source));
  ASSERT_THAT(zip, IsOk());
  Result<std::unique_ptr<ReaderSeeker>> member = zip->OpenReadOnly("boot.img");
  ASSERT_THAT(member, IsOk());
  EXPECT_THAT(ReadToString(**member), IsOkAndValue("boot bytes"));
  EXPECT_TRUE(http_client.RequestMade(kMediaUrl));
  // The listing reported the size, so the reader has nothing left to ask.
  EXPECT_FALSE(zip_handler->HeadRequestMade());
}

TEST(GcsBuildApiTests, DownloadFileExtractsAnArchiveMemberSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);

  Result<ZipOverRanges> zip_handler = ZipOverRanges::Create(
      {{"cvd-host_package.tar.gz", "package bytes"}, {"boot.img", "boot"}});
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kMediaUrl);
  http_client.SetResponse(
      absl::StrCat(R"({"size": ")", zip_handler->Size(), R"("})"), kObjectUrl);

  GcsBuildString build_string = {
      .url = "gs://bucket/dist/phone-img-1.zip",
      .filepath = "cvd-host_package.tar.gz",
  };
  Result<GcsBuild> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  std::string expected_path =
      std::string(target_directory.path) + "/cvd-host_package.tar.gz";
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path,
                               "cvd-host_package.tar.gz"),
              IsOkAndValue(expected_path));

  EXPECT_THAT(ReadFileContents(expected_path), IsOkAndValue("package bytes"));
  EXPECT_TRUE(zip_handler->RangeRequestMade());
  EXPECT_FALSE(zip_handler->HeadRequestMade());
}

}  // namespace
}  // namespace cuttlefish

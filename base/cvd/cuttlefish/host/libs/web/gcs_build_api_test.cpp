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

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/host/libs/zip/zip_string.h"
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

bool HasAuthorization(const std::vector<std::string>& headers) {
  for (const std::string& header : headers) {
    if (absl::StartsWith(header, "Authorization:")) {
      return true;
    }
  }
  return false;
}

// Serves one archive over HTTP range requests, as Cloud Storage does.
class ZipOverRanges {
 public:
  static Result<ZipOverRanges> Create(
      const std::map<std::string, std::string>& contents) {
    std::string buffer(4096, '\0');

    WritableZipSource source =
        CF_EXPECT(WritableZipSource::BorrowData(buffer.data(), buffer.size()));
    WritableZip zip = CF_EXPECT(WritableZip::FromSource(std::move(source)));
    for (const auto& [path, data] : contents) {
      CF_EXPECT(AddStringAt(zip, data, path));
    }
    source = CF_EXPECT(WritableZipSource::FromZip(std::move(zip)));

    return ZipOverRanges(CF_EXPECT(ReadToString(source)));
  }

  HttpResponse<std::string> operator()(const HttpRequest& request) {
    static constexpr std::string_view kPrefix = "Range: bytes=";
    size_t start = 0;
    size_t end = data_.size();
    for (const std::string& header : request.headers) {
      if (!absl::StartsWith(header, kPrefix)) {
        continue;
      }
      *ranged_ = true;
      const std::string range = header.substr(kPrefix.size());
      std::vector<std::string_view> parts = absl::StrSplit(range, '-');
      if (parts.size() == 2 && absl::SimpleAtoi(parts[0], &start) &&
          absl::SimpleAtoi(parts[1], &end)) {
        end++;  // HTTP ranges are inclusive at both ends
      }
    }
    if (end > data_.size()) {
      end = data_.size();
    }
    return HttpResponse<std::string>{
        .data = data_.substr(start, end - start),
        .http_code = 200,
        .headers =
            {
                {"content-length", std::to_string(end - start)},
                {"accept-ranges", "bytes"},
            },
    };
  }

  bool RangeRequestMade() const { return *ranged_; }

 private:
  explicit ZipOverRanges(std::string data)
      : data_(std::move(data)), ranged_(std::make_shared<bool>(false)) {}

  std::string data_;
  std::shared_ptr<bool> ranged_;
};

TEST(GcsBuildApiTests, GetBuildProbesObjectMetadataSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"size": "3", "generation": "17", "md5Hash": "m"})", kObjectUrl);

  BuildString build_string =
      GcsBuildString{.url = "gs://bucket/dist/phone-img-1.zip"};
  Result<Build> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  const GcsBuild* gcs = std::get_if<GcsBuild>(&*build);
  ASSERT_NE(gcs, nullptr);
  EXPECT_EQ(gcs->object, "phone-img-1.zip");
  EXPECT_EQ(gcs->generation, "17");
  EXPECT_EQ(gcs->md5, "m");
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

  BuildString build_string =
      GcsBuildString{.url = "gs://bucket/dist/phone-img-1.zip"};
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

  BuildString build_string =
      GcsBuildString{.url = "gs://bucket/dist/phone-img-1.zip"};
  ASSERT_THAT(api.GetBuild(build_string), IsOk());
  EXPECT_FALSE(authorized);
}

TEST(GcsBuildApiTests, GetBuildMissingObjectFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      HttpResponse<std::string>{.data = "{}", .http_code = 404}, kObjectUrl);

  BuildString build_string =
      GcsBuildString{.url = "gs://bucket/dist/phone-img-1.zip"};
  EXPECT_THAT(
      api.GetBuild(build_string),
      IsErrorAndMessage(AllOf(HasSubstr("gs://bucket/dist/phone-img-1.zip"),
                              HasSubstr("404"), HasSubstr("Check log file"))));
}

TEST(GcsBuildApiTests, GetBuildEmptyPrefixFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(R"({"kind": "storage#objects"})", kListUrl);

  BuildString build_string = GcsBuildString{.url = "gs://bucket/dist/"};
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
                             "md5Hash": "mb"}, {"name": "dist/"}]})";
        }
        return HttpResponse<std::string>{.data = data, .http_code = 200};
      },
      kListUrl);

  BuildString build_string = GcsBuildString{.url = "gs://bucket/dist/"};
  Result<Build> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  const GcsBuild* gcs = std::get_if<GcsBuild>(&*build);
  ASSERT_NE(gcs, nullptr);
  EXPECT_THAT(gcs->contents, SizeIs(2));
  EXPECT_EQ(gcs->contents.at("a.txt").generation, "1");
  EXPECT_EQ(gcs->contents.at("b.txt").md5, "mb");
  EXPECT_TRUE(http_client.RequestMade("pageToken=page2"));
}

TEST(GcsBuildApiTests, DownloadFileAbsentArtifactFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"items": [{"name": "dist/a.txt", "generation": "1"}]})", kListUrl);

  BuildString build_string = GcsBuildString{.url = "gs://bucket/dist/"};
  Result<Build> build = api.GetBuild(build_string);
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

  BuildString build_string = GcsBuildString{.url = "gs://bucket/dist/"};
  Result<Build> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  std::string expected_path =
      std::string(target_directory.path) + "/misc_info.txt";
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path, "misc_info.txt"),
              IsOkAndValue(expected_path));

  std::string contents;
  ASSERT_TRUE(android::base::ReadFileToString(expected_path, &contents));
  EXPECT_EQ(contents, "recovery_api_version=3");
}

TEST(GcsBuildApiTests, FileReaderReadsTheNamedArtifactSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(
      R"({"items": [{"name": "dist/phone-img-1.zip", "generation": "1"}]})",
      kListUrl);

  Result<ZipOverRanges> zip_handler =
      ZipOverRanges::Create({{"boot.img", "boot bytes"}});
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kMediaUrl);

  BuildString build_string = GcsBuildString{.url = "gs://bucket/dist/"};
  Result<Build> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  Result<SeekableZipSource> source = api.FileReader(*build, "phone-img-1.zip");
  ASSERT_THAT(source, IsOk());
  Result<ReadableZip> zip = ReadableZip::FromSource(std::move(*source));
  ASSERT_THAT(zip, IsOk());
  Result<std::unique_ptr<ReaderSeeker>> member = zip->OpenReadOnly("boot.img");
  ASSERT_THAT(member, IsOk());
  EXPECT_THAT(ReadToString(**member), IsOkAndValue("boot bytes"));
  EXPECT_TRUE(http_client.RequestMade(kMediaUrl));
}

TEST(GcsBuildApiTests, DownloadFileExtractsAnArchiveMemberSuccess) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);
  http_client.SetResponse(R"({"size": "3"})", kObjectUrl);

  Result<ZipOverRanges> zip_handler = ZipOverRanges::Create(
      {{"cvd-host_package.tar.gz", "package bytes"}, {"boot.img", "boot"}});
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kMediaUrl);

  BuildString build_string = GcsBuildString{
      .url = "gs://bucket/dist/phone-img-1.zip",
      .filepath = "cvd-host_package.tar.gz",
  };
  Result<Build> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  TemporaryDir target_directory;
  std::string expected_path =
      std::string(target_directory.path) + "/cvd-host_package.tar.gz";
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path,
                               "cvd-host_package.tar.gz"),
              IsOkAndValue(expected_path));

  std::string contents;
  ASSERT_TRUE(android::base::ReadFileToString(expected_path, &contents));
  EXPECT_EQ(contents, "package bytes");
  EXPECT_TRUE(zip_handler->RangeRequestMade());
}

TEST(GcsBuildApiTests, ForeignBuildFail) {
  FakeHttpClient http_client;
  GcsBuildApi api(http_client, nullptr);

  BuildString build_string = DeviceBuildString{.branch_or_id = "aosp-main"};
  EXPECT_THAT(api.GetBuild(build_string),
              IsErrorAndMessage(HasSubstr("cannot handle")));
  EXPECT_THAT(api.FileReader(DeviceBuild{.id = "1", .target = "t"}, "a.zip"),
              IsErrorAndMessage(HasSubstr("cannot handle")));
}

}  // namespace
}  // namespace cuttlefish

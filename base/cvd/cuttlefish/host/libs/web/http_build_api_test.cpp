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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
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
using ::testing::Not;

constexpr char kObjectUrl[] = "https://example.com/dist/phone-img-1.zip";
constexpr char kSignedUrl[] =
    "https://example.com/dist/phone-img-1.zip?X-Goog-Signature=secret";

bool HasAuthorization(const std::vector<std::string>& headers) {
  for (const std::string& header : headers) {
    if (absl::StartsWith(header, "Authorization:")) {
      return true;
    }
  }
  return false;
}

// Serves one archive over HTTP range requests.
class ZipOverRanges {
 public:
  static Result<ZipOverRanges> Create(
      const std::map<std::string, std::string>& contents, bool serve_ranges,
      bool reject_head = false) {
    std::string buffer(4096, '\0');

    WritableZipSource source =
        CF_EXPECT(WritableZipSource::BorrowData(buffer.data(), buffer.size()));
    WritableZip zip = CF_EXPECT(WritableZip::FromSource(std::move(source)));
    for (const auto& [path, data] : contents) {
      CF_EXPECT(AddStringAt(zip, data, path));
    }
    source = CF_EXPECT(WritableZipSource::FromZip(std::move(zip)));

    return ZipOverRanges(CF_EXPECT(ReadToString(source)), serve_ranges,
                         reject_head);
  }

  HttpResponse<std::string> operator()(const HttpRequest& request) {
    static constexpr std::string_view kPrefix = "Range: bytes=";
    if (request.method == HttpMethod::kHead) {
      *head_ = true;
      if (reject_head_) {
        return HttpResponse<std::string>{.http_code = 403};
      }
    }
    size_t start = 0;
    size_t end = data_.size();
    for (const std::string& header : request.headers) {
      if (!absl::StartsWith(header, kPrefix) || !serve_ranges_) {
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
    std::vector<HttpHeader> headers = {
        {"content-length", std::to_string(end - start)},
        {"etag", "\"abc\""},
    };
    if (serve_ranges_) {
      headers.push_back({"accept-ranges", "bytes"});
      headers.push_back(
          {"content-range",
           absl::StrCat("bytes ", start, "-", end - 1, "/", data_.size())});
    }
    return HttpResponse<std::string>{
        .data = data_.substr(start, end - start),
        .http_code = 200,
        .headers = std::move(headers),
    };
  }

  bool RangeRequestMade() const { return *ranged_; }
  bool HeadRequestMade() const { return *head_; }

 private:
  ZipOverRanges(std::string data, bool serve_ranges, bool reject_head)
      : data_(std::move(data)),
        serve_ranges_(serve_ranges),
        reject_head_(reject_head),
        ranged_(std::make_shared<bool>(false)),
        head_(std::make_shared<bool>(false)) {}

  std::string data_;
  bool serve_ranges_;
  bool reject_head_;
  std::shared_ptr<bool> ranged_;
  std::shared_ptr<bool> head_;
};

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
            .headers = {{"etag", "\"v1\""},
                        {"accept-ranges", "bytes"},
                        {"content-range", "bytes 0-0/4096"}},
        };
      },
      kObjectUrl);

  BuildString build_string = HttpBuildString{.url = kSignedUrl};
  Result<Build> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  const HttpBuild* http = std::get_if<HttpBuild>(&*build);
  ASSERT_NE(http, nullptr);
  EXPECT_EQ(http->url, kSignedUrl);
  EXPECT_EQ(http->etag, "\"v1\"");
  EXPECT_TRUE(http->accept_ranges);
  EXPECT_EQ(http->size, 4096);
  // A pre-signed URL signs the verb, so the probe is a ranged GET.
  EXPECT_THAT(seen_headers, ::testing::Contains("Range: bytes=0-0"));
  EXPECT_FALSE(HasAuthorization(seen_headers));
}

TEST(HttpBuildApiTests, GetBuildDoesNotProbeADirectorySuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  BuildString build_string =
      HttpBuildString{.url = "https://example.com/dist/"};
  Result<Build> build = api.GetBuild(build_string);

  ASSERT_THAT(build, IsOk());
  EXPECT_FALSE(http_client.RequestMade("https://example.com/dist/"));
}

TEST(HttpBuildApiTests, GetBuildMissingUrlFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);
  http_client.SetResponse(HttpResponse<std::string>{.http_code = 404},
                          kObjectUrl);

  BuildString build_string = HttpBuildString{.url = kSignedUrl};
  EXPECT_THAT(api.GetBuild(build_string),
              IsErrorAndMessage(AllOf(HasSubstr(kObjectUrl), HasSubstr("404"),
                                      Not(HasSubstr("secret")))));
}

TEST(HttpBuildApiTests, GetBuildRedirectFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);
  http_client.SetResponse(HttpResponse<std::string>{.http_code = 302},
                          kObjectUrl);

  BuildString build_string = HttpBuildString{.url = kSignedUrl};
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

  BuildString build_string =
      HttpBuildString{.url = "https://example.com/dist/"};
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
  EXPECT_FALSE(authorized);
}

TEST(HttpBuildApiTests, DownloadFileAbsentFromADirectoryFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  BuildString build_string =
      HttpBuildString{.url = "https://example.com/dist/"};
  Result<Build> build = api.GetBuild(build_string);
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

  BuildString build_string = HttpBuildString{.url = kSignedUrl};
  Result<Build> build = api.GetBuild(build_string);
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

  BuildString build_string = HttpBuildString{.url = kSignedUrl};
  Result<Build> build = api.GetBuild(build_string);
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
  // The probe reported the size, so the reader has nothing left to ask.
  EXPECT_FALSE(zip_handler->HeadRequestMade());
}

TEST(HttpBuildApiTests, FileReaderReadsAnOriginThatRejectsHeadSuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  Result<ZipOverRanges> zip_handler =
      ZipOverRanges::Create({{"boot.img", "boot bytes"}},
                            /*serve_ranges=*/true, /*reject_head=*/true);
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kObjectUrl);

  BuildString build_string = HttpBuildString{.url = kSignedUrl};
  Result<Build> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());

  Result<SeekableZipSource> source = api.FileReader(*build, "phone-img-1.zip");
  ASSERT_THAT(source, IsOk());
  Result<ReadableZip> zip = ReadableZip::FromSource(std::move(*source));
  ASSERT_THAT(zip, IsOk());
  Result<std::unique_ptr<ReaderSeeker>> member = zip->OpenReadOnly("boot.img");
  ASSERT_THAT(member, IsOk());
  EXPECT_THAT(ReadToString(**member), IsOkAndValue("boot bytes"));
  EXPECT_FALSE(zip_handler->HeadRequestMade());
}

TEST(HttpBuildApiTests, DownloadFileExtractsAnArchiveMemberSuccess) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  Result<ZipOverRanges> zip_handler = ZipOverRanges::Create(
      {{"cvd-host_package.tar.gz", "package bytes"}, {"boot.img", "boot"}},
      /*serve_ranges=*/true);
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kObjectUrl);

  BuildString build_string = HttpBuildString{
      .url = kSignedUrl,
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
  EXPECT_FALSE(zip_handler->HeadRequestMade());
}

TEST(HttpBuildApiTests, DownloadFileMemberWithoutRangeSupportFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  Result<ZipOverRanges> zip_handler = ZipOverRanges::Create(
      {{"cvd-host_package.tar.gz", "package bytes"}}, /*serve_ranges=*/false);
  ASSERT_THAT(zip_handler, IsOk());
  http_client.SetResponse(*zip_handler, kObjectUrl);

  BuildString build_string = HttpBuildString{
      .url = kSignedUrl,
      .filepath = "cvd-host_package.tar.gz",
  };
  Result<Build> build = api.GetBuild(build_string);
  ASSERT_THAT(build, IsOk());
  EXPECT_FALSE(std::get<HttpBuild>(*build).accept_ranges);

  TemporaryDir target_directory;
  EXPECT_THAT(api.DownloadFile(*build, target_directory.path,
                               "cvd-host_package.tar.gz"),
              IsErrorAndMessage(AllOf(HasSubstr("range requests"),
                                      HasSubstr("cvd-host_package.tar.gz"))));
}

TEST(HttpBuildApiTests, ForeignBuildFail) {
  FakeHttpClient http_client;
  HttpBuildApi api(http_client);

  BuildString build_string = DeviceBuildString{.branch_or_id = "aosp-main"};
  EXPECT_THAT(api.GetBuild(build_string),
              IsErrorAndMessage(HasSubstr("cannot handle")));
  EXPECT_THAT(api.FileReader(DeviceBuild{.id = "1", .target = "t"}, "a.zip"),
              IsErrorAndMessage(HasSubstr("cannot handle")));
}

}  // namespace
}  // namespace cuttlefish

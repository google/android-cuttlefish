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

#include "cuttlefish/host/libs/web/url_download.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/file.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::SizeIs;

constexpr char kUrl[] = "https://example.com/dist/phone-img-1.zip";
constexpr char kContents[] = "0123456789ABCDEF";
constexpr char kRangePrefix[] = "Range: bytes=";
constexpr char kVersion[] = "\"v1\"";
constexpr size_t kSize = 16;

std::optional<std::pair<size_t, size_t>> RequestedRange(
    const std::vector<std::string>& headers) {
  for (const std::string& header : headers) {
    std::string_view range = header;
    if (!absl::ConsumePrefix(&range, kRangePrefix)) {
      continue;
    }
    const std::vector<std::string_view> ends = absl::StrSplit(range, '-');
    size_t start = 0;
    size_t last = 0;
    EXPECT_EQ(ends.size(), 2);
    EXPECT_TRUE(absl::SimpleAtoi(ends[0], &start));
    EXPECT_TRUE(absl::SimpleAtoi(ends[1], &last));
    return std::make_pair(start, last + 1);
  }
  return std::nullopt;
}

class UrlDownloadTests : public ::testing::Test {
 protected:
  std::string Path() const { return absl::StrCat(directory_.path, "/img.zip"); }
  std::string PartPath(std::string_view version = kVersion) const {
    return PartialFilePath(Path(), version);
  }

  void WritePart(const std::string& contents,
                 std::string_view version = kVersion) {
    ASSERT_THAT(WriteNewFile(PartPath(version), contents), IsOk());
  }

  // Serves whatever range is asked for, unless it is told to ignore ranges as
  // an origin does when `If-Range` does not match what it holds.
  void ServeContents(bool honor_ranges = true) {
    http_client_.SetResponse(
        [this, honor_ranges](const HttpRequest& request) {
          requests_.push_back(request.headers);
          const std::optional<std::pair<size_t, size_t>> range =
              RequestedRange(request.headers);
          if (!honor_ranges || !range.has_value()) {
            return HttpResponse<std::string>{.data = kContents,
                                             .http_code = 200};
          }
          return HttpResponse<std::string>{
              .data = std::string(kContents).substr(
                  range->first, range->second - range->first),
              .http_code = 206,
          };
        },
        kUrl);
  }

  FakeHttpClient http_client_;
  TemporaryDir directory_;
  std::vector<std::vector<std::string>> requests_;
};

TEST_F(UrlDownloadTests, WithoutAVersionTheWholeObjectIsDownloadedSuccess) {
  ServeContents();
  UrlDownload download = {.url = kUrl, .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_THAT(ReadFileContents(Path()), IsOkAndValue(kContents));
  EXPECT_THAT(requests_, SizeIs(1));
  EXPECT_THAT(requests_[0], Not(Contains(HasSubstr("Range:"))));
  EXPECT_FALSE(FileExists(PartPath()));
}

TEST_F(UrlDownloadTests, APartialFileIsResumedSuccess) {
  ServeContents();
  WritePart("012345");
  UrlDownload download = {
      .url = kUrl,
      .if_range = kVersion,
      .version = kVersion,
      .size = kSize,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_THAT(ReadFileContents(Path()), IsOkAndValue(kContents));
  EXPECT_THAT(requests_, SizeIs(1));
  EXPECT_THAT(requests_[0], AllOf(Contains("Range: bytes=6-15"),
                                  Contains("If-Range: \"v1\"")));
  EXPECT_FALSE(FileExists(PartPath()));
}

TEST_F(UrlDownloadTests, AVersionedUrlResumesWithoutIfRangeSuccess) {
  ServeContents();
  WritePart("012345", "17");
  UrlDownload download = {.url = kUrl, .version = "17", .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_THAT(ReadFileContents(Path()), IsOkAndValue(kContents));
  EXPECT_THAT(requests_[0], AllOf(Contains("Range: bytes=6-15"),
                                  Not(Contains(HasSubstr("If-Range:")))));
}

TEST_F(UrlDownloadTests, APartialFileOfAnotherVersionIsNotResumedSuccess) {
  ServeContents();
  WritePart("xxxxxx", "\"v0\"");
  UrlDownload download = {
      .url = kUrl,
      .if_range = kVersion,
      .version = kVersion,
      .size = kSize,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_THAT(ReadFileContents(Path()), IsOkAndValue(kContents));
  EXPECT_THAT(requests_[0], Contains("Range: bytes=0-15"));
}

TEST_F(UrlDownloadTests, APartialFileLongerThanTheObjectIsDiscardedSuccess) {
  ServeContents();
  WritePart(std::string(kSize + 8, 'z'));
  UrlDownload download = {
      .url = kUrl,
      .if_range = kVersion,
      .version = kVersion,
      .size = kSize,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_THAT(ReadFileContents(Path()), IsOkAndValue(kContents));
  EXPECT_THAT(requests_[0], Contains("Range: bytes=0-15"));
}

TEST_F(UrlDownloadTests, AnUnmeasuredObjectIsNotResumedSuccess) {
  ServeContents();
  WritePart("zzzz");
  UrlDownload download = {
      .url = kUrl,
      .if_range = kVersion,
      .version = kVersion,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_THAT(ReadFileContents(Path()), IsOkAndValue(kContents));
  EXPECT_THAT(requests_[0], Not(Contains(HasSubstr("Range:"))));
}

TEST_F(UrlDownloadTests, AChangedObjectFailsTheResumedDownloadFail) {
  ServeContents(/*honor_ranges=*/false);
  WritePart("xxxxxx");
  UrlDownload download = {
      .url = kUrl,
      .if_range = kVersion,
      .version = kVersion,
      .size = kSize,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()),
              IsErrorAndMessage(HasSubstr(kUrl)));
  EXPECT_THAT(requests_[0], Contains("If-Range: \"v1\""));
  EXPECT_FALSE(FileExists(Path()));
}

TEST_F(UrlDownloadTests, APartialFileIsHeldUnderALockSuccess) {
  bool locked_out = false;
  http_client_.SetResponse(
      [this, &locked_out](const HttpRequest&) {
        Result<Fd> other = Fd::Open(PartPath(), O_RDWR);
        locked_out =
            other.has_value() && !other->Flock(LOCK_EX | LOCK_NB).has_value();
        return HttpResponse<std::string>{.data = kContents, .http_code = 206};
      },
      kUrl);
  UrlDownload download = {.url = kUrl, .version = kVersion, .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_TRUE(locked_out);
}

TEST_F(UrlDownloadTests, AMissingObjectLeavesNoPartialFileFail) {
  UrlDownload download = {.url = kUrl, .version = kVersion, .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()),
              IsErrorAndMessage(HasSubstr(kUrl)));
  EXPECT_FALSE(FileExists(PartPath()));
  EXPECT_FALSE(FileExists(Path()));
}

}  // namespace
}  // namespace cuttlefish

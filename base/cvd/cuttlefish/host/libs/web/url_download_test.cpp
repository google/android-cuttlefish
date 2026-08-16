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

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "android-base/file.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
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
constexpr size_t kSize = 16;

size_t RangeStart(const std::vector<std::string>& headers) {
  static constexpr std::string_view kPrefix = "Range: bytes=";
  size_t start = 0;
  for (const std::string& header : headers) {
    if (!absl::StartsWith(header, kPrefix)) {
      continue;
    }
    const std::string range = header.substr(kPrefix.size());
    EXPECT_TRUE(absl::SimpleAtoi(range.substr(0, range.find('-')), &start));
  }
  return start;
}

class UrlDownloadTests : public ::testing::Test {
 protected:
  std::string Path() const { return std::string(directory_.path) + "/img.zip"; }
  std::string PartPath() const { return Path() + ".part"; }

  void WritePart(const std::string& contents) {
    ASSERT_TRUE(android::base::WriteStringToFile(contents, PartPath()));
  }

  std::string Downloaded() const {
    std::string contents;
    EXPECT_TRUE(android::base::ReadFileToString(Path(), &contents));
    return contents;
  }

  // Serves whatever range is asked for, unless it is told to ignore ranges as
  // an origin does when `If-Range` does not match what it holds.
  void ServeContents(bool honor_ranges = true) {
    http_client_.SetResponse(
        [this, honor_ranges](const HttpRequest& request) {
          requests_.push_back(request.headers);
          size_t start = honor_ranges ? RangeStart(request.headers) : 0;
          return HttpResponse<std::string>{
              .data = std::string(kContents).substr(start),
              .http_code = start > 0 ? 206 : 200,
          };
        },
        kUrl);
  }

  FakeHttpClient http_client_;
  TemporaryDir directory_;
  std::vector<std::vector<std::string>> requests_;
};

TEST_F(UrlDownloadTests, WithoutAValidatorTheWholeObjectIsDownloadedSuccess) {
  ServeContents();
  UrlDownload download = {.url = kUrl, .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_EQ(Downloaded(), kContents);
  EXPECT_THAT(requests_, SizeIs(1));
  EXPECT_THAT(requests_[0], Not(Contains(HasSubstr("Range:"))));
  EXPECT_FALSE(FileExists(PartPath()));
}

TEST_F(UrlDownloadTests, APartialFileIsResumedSuccess) {
  ServeContents();
  WritePart("012345");
  UrlDownload download = {
      .url = kUrl,
      .if_range = "\"v1\"",
      .resumable = true,
      .size = kSize,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_EQ(Downloaded(), kContents);
  EXPECT_THAT(requests_, SizeIs(1));
  EXPECT_THAT(requests_[0],
              AllOf(Contains("Range: bytes=6-"), Contains("If-Range: \"v1\"")));
  EXPECT_FALSE(FileExists(PartPath()));
}

TEST_F(UrlDownloadTests, AVersionedUrlResumesWithoutIfRangeSuccess) {
  ServeContents();
  WritePart("012345");
  UrlDownload download = {.url = kUrl, .resumable = true, .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_EQ(Downloaded(), kContents);
  EXPECT_THAT(requests_[0], AllOf(Contains("Range: bytes=6-"),
                                  Not(Contains(HasSubstr("If-Range:")))));
}

TEST_F(UrlDownloadTests, APartialFileLongerThanTheObjectIsDiscardedSuccess) {
  ServeContents();
  WritePart(std::string(kSize + 8, 'z'));
  UrlDownload download = {
      .url = kUrl,
      .if_range = "\"v1\"",
      .resumable = true,
      .size = kSize,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_EQ(Downloaded(), kContents);
  EXPECT_THAT(requests_[0], Not(Contains(HasSubstr("Range:"))));
}

TEST_F(UrlDownloadTests, APartialFileOfAnUnmeasuredObjectIsDiscardedSuccess) {
  ServeContents();
  WritePart("zzzz");
  UrlDownload download = {
      .url = kUrl,
      .if_range = "\"v1\"",
      .resumable = true,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_EQ(Downloaded(), kContents);
  EXPECT_THAT(requests_[0], Not(Contains(HasSubstr("Range:"))));
}

TEST_F(UrlDownloadTests, AChangedObjectIsDownloadedAgainSuccess) {
  ServeContents(/*honor_ranges=*/false);
  WritePart("xxxxxx");
  UrlDownload download = {
      .url = kUrl,
      .if_range = "\"v1\"",
      .resumable = true,
      .size = kSize,
  };

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_EQ(Downloaded(), kContents);
  EXPECT_THAT(requests_, SizeIs(2));
  EXPECT_THAT(requests_[1], Not(Contains(HasSubstr("Range:"))));
  EXPECT_FALSE(FileExists(PartPath()));
}

TEST_F(UrlDownloadTests, APartialFileIsHeldUnderALockSuccess) {
  bool locked_out = false;
  http_client_.SetResponse(
      [this, &locked_out](const HttpRequest&) {
        SharedFD other = SharedFD::Open(PartPath(), O_RDWR);
        locked_out =
            other->IsOpen() && !other->Flock(LOCK_EX | LOCK_NB).has_value();
        return HttpResponse<std::string>{.data = kContents, .http_code = 200};
      },
      kUrl);
  UrlDownload download = {.url = kUrl, .resumable = true, .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()), IsOk());
  EXPECT_TRUE(locked_out);
}

TEST_F(UrlDownloadTests, AnOpenFileIsFoundAtItsPathSuccess) {
  WritePart("012345");
  SharedFD part = SharedFD::Open(PartPath(), O_RDWR);
  ASSERT_TRUE(part->IsOpen());

  EXPECT_THAT(HoldsFileAt(part, PartPath()), IsOkAndValue(true));
}

TEST_F(UrlDownloadTests, AnOpenFileRenamedAwayIsNotFoundSuccess) {
  WritePart("012345");
  SharedFD part = SharedFD::Open(PartPath(), O_RDWR);
  ASSERT_TRUE(part->IsOpen());
  ASSERT_THAT(RenameFile(PartPath(), Path()), IsOk());

  EXPECT_THAT(HoldsFileAt(part, PartPath()), IsOkAndValue(false));
}

TEST_F(UrlDownloadTests, AnOpenFileReplacedAtItsPathIsNotFoundSuccess) {
  WritePart("012345");
  SharedFD part = SharedFD::Open(PartPath(), O_RDWR);
  ASSERT_TRUE(part->IsOpen());
  ASSERT_THAT(RenameFile(PartPath(), Path()), IsOk());
  WritePart("6789");

  EXPECT_THAT(HoldsFileAt(part, PartPath()), IsOkAndValue(false));
}

TEST_F(UrlDownloadTests, AMissingObjectLeavesNoPartialFileFail) {
  UrlDownload download = {.url = kUrl, .resumable = true, .size = kSize};

  EXPECT_THAT(DownloadUrlToFile(http_client_, download, Path()),
              IsErrorAndMessage(AllOf(HasSubstr(kUrl), HasSubstr("404"))));
  EXPECT_FALSE(FileExists(PartPath()));
  EXPECT_FALSE(FileExists(Path()));
}

}  // namespace
}  // namespace cuttlefish

//
// Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/libs/zip/remote_zip.h"

#include <stdlib.h>

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"
#include "cuttlefish/host/libs/zip/zip_string.h"

namespace cuttlefish {
namespace {

class HttpCallback {
 public:
  static Result<HttpCallback> Create(
      const std::map<std::string, std::string>& contents) {
    std::string data(4096, '\0');

    WritableZipSource source =
        CF_EXPECT(WritableZipSource::BorrowData(data.data(), data.size()));
    WritableZip zip = CF_EXPECT(WritableZip::FromSource(std::move(source)));

    for (const auto& [path, data] : contents) {
      CF_EXPECT(AddStringAt(zip, data, path));
    }

    source = CF_EXPECT(WritableZipSource::FromZip(std::move(zip)));

    return HttpCallback(CF_EXPECT(ReadToString(source)));
  }

  std::string operator()(const HttpRequest& request) {
    static constexpr std::string_view kPrefix = "Range: bytes=";
    std::string range;
    for (const std::string& header : request.headers) {
      if (absl::StartsWith(header, kPrefix)) {
        range = header.substr(kPrefix.size());
      }
    }
    size_t start = 0;
    size_t end = data_.size();
    if (!range.empty()) {
      std::vector<std::string_view> range_parts = absl::StrSplit(range, "-");
      if (range_parts.size() == 2) {
        if (!absl::SimpleAtoi(range_parts[0], &start) ||
            !absl::SimpleAtoi(range_parts[1], &end)) {
          start = 0;
          end = data_.size();
        }
      }
    }
    if (end > data_.size()) {
      end = data_.size();
    }
    return data_.substr(start, end - start);
  }

  size_t Size() { return data_.size(); }

 private:
  HttpCallback(std::string data) : data_(std::move(data)) {}

  std::string data_;
};

TEST(RemoteZipTest, TwoFiles) {
  FakeHttpClient http_client;

  std::map<std::string, std::string> zip_contents = {
      std::make_pair("a.txt", "abc"), std::make_pair("b.txt", "def")};

  Result<HttpCallback> callback = HttpCallback::Create(zip_contents);
  ASSERT_THAT(callback, IsOk());
  size_t size = callback->Size();

  http_client.SetResponse(std::move(*callback));

  Result<ReadableZip> remote_zip = ZipFromUrl(http_client, "url", size, {});
  ASSERT_THAT(remote_zip, IsOk());

  Result<SeekableZipSource> file_a(remote_zip->GetFile("a.txt"));
  ASSERT_THAT(file_a, IsOk());
  ASSERT_THAT(ReadToString(*file_a), IsOkAndValue("abc"));

  Result<SeekableZipSource> file_b(remote_zip->GetFile("b.txt"));
  ASSERT_THAT(file_b, IsOk());
  ASSERT_THAT(ReadToString(*file_b), IsOkAndValue("def"));
}

}  // namespace
}  // namespace cuttlefish

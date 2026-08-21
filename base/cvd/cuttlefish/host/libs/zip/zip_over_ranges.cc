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

#include "cuttlefish/host/libs/zip/zip_over_ranges.h"

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "fmt/format.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/host/libs/zip/zip_string.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<ZipOverRanges> ZipOverRanges::Create(
    const std::map<std::string, std::string>& contents, bool serve_ranges,
    bool reject_head) {
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

HttpResponse<std::string> ZipOverRanges::operator()(
    const HttpRequest& request) {
  static constexpr std::string_view kPrefix = "Range: bytes=";
  std::vector<HttpHeader> headers = {{"etag", "\"abc\""}};
  if (serve_ranges_) {
    headers.push_back({"accept-ranges", "bytes"});
  }
  if (request.method == HttpMethod::kHead) {
    ++*heads_;
    if (reject_head_) {
      return HttpResponse<std::string>{.http_code = 403};
    }
    headers.push_back({"content-length", std::to_string(data_.size())});
    return HttpResponse<std::string>{
        .http_code = 200,
        .headers = std::move(headers),
    };
  }
  size_t start = 0;
  size_t end = data_.size();
  bool ranged = false;
  for (const std::string& header : request.headers) {
    std::string_view range = header;
    if (!absl::ConsumePrefix(&range, kPrefix) || !serve_ranges_) {
      continue;
    }
    const std::vector<std::string_view> parts = absl::StrSplit(range, '-');
    if (parts.size() == 2 && absl::SimpleAtoi(parts[0], &start) &&
        absl::SimpleAtoi(parts[1], &end)) {
      end++;  // HTTP ranges are inclusive at both ends
      ranged = true;
    } else {
      start = 0;
      end = data_.size();
    }
  }
  if (end > data_.size()) {
    end = data_.size();
  }
  headers.push_back({"content-length", std::to_string(end - start)});
  if (ranged) {
    *ranged_ = true;
    headers.push_back({"content-range", fmt::format("bytes {}-{}/{}", start,
                                                    end - 1, data_.size())});
  }
  return HttpResponse<std::string>{
      .data = data_.substr(start, end - start),
      .http_code = ranged ? 206 : 200,
      .headers = std::move(headers),
  };
}

ZipOverRanges::ZipOverRanges(std::string data, bool serve_ranges,
                             bool reject_head)
    : data_(std::move(data)),
      serve_ranges_(serve_ranges),
      reject_head_(reject_head),
      ranged_(std::make_shared<bool>(false)),
      heads_(std::make_shared<int>(0)) {}

}  // namespace cuttlefish

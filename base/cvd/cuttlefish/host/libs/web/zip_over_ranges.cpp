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

#include "cuttlefish/host/libs/web/zip_over_ranges.h"

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/host/libs/zip/zip_string.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

bool HasAuthorization(const std::vector<std::string>& headers) {
  for (const std::string& header : headers) {
    if (absl::StartsWith(header, "Authorization:")) {
      return true;
    }
  }
  return false;
}

Result<ZipOverRanges> ZipOverRanges::Create(
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

HttpResponse<std::string> ZipOverRanges::operator()(
    const HttpRequest& request) {
  static constexpr std::string_view kPrefix = "Range: bytes=";
  size_t start = 0;
  size_t end = data_.size();
  for (const std::string& header : request.headers) {
    if (!absl::StartsWith(header, kPrefix)) {
      continue;
    }
    *ranged_ = true;
    const std::string range = header.substr(kPrefix.size());
    const std::vector<std::string_view> parts = absl::StrSplit(range, '-');
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

ZipOverRanges::ZipOverRanges(std::string data)
    : data_(std::move(data)), ranged_(std::make_shared<bool>(false)) {}

}  // namespace cuttlefish

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

#include "cuttlefish/host/libs/web/http_client/http_probe.h"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr long kPartialContent = 206;
constexpr long kForbidden = 403;
constexpr long kMethodNotAllowed = 405;

Result<HttpResponse<void>> Probe(HttpClient& http_client, HttpMethod method,
                                 const std::string& url,
                                 std::vector<std::string> headers) {
  const HttpRequest request = {
      .method = method,
      .url = url,
      .headers = std::move(headers),
  };
  auto discard = [](char*, size_t) -> Result<void> { return {}; };
  return CF_EXPECT(http_client.DownloadToCallback(request, discard));
}

bool ServesRanges(const HttpResponse<void>& response) {
  if (response.http_code == kPartialContent) {
    return true;
  }
  const std::optional<std::string_view> ranges =
      HeaderValue(response.headers, "accept-ranges");
  return ranges.has_value() && absl::StrContains(*ranges, "bytes");
}

// The whole object's length: the total of a partial response's `Content-Range`
// or, where the origin answered the range request with the whole object, its
// `Content-Length`.
std::optional<uint64_t> ProbedSize(const HttpResponse<void>& response) {
  uint64_t size = 0;
  if (std::optional<std::string_view> range =
          HeaderValue(response.headers, "content-range")) {
    const size_t total = range->rfind('/');
    if (total != std::string_view::npos &&
        absl::SimpleAtoi(range->substr(total + 1), &size)) {
      return size;
    }
  }
  const std::optional<std::string_view> length =
      HeaderValue(response.headers, "content-length");
  if (response.http_code != kPartialContent && length.has_value() &&
      absl::SimpleAtoi(*length, &size)) {
    return size;
  }
  return std::nullopt;
}

}  // namespace

Result<HttpObjectInfo> ProbeHttpObject(
    HttpClient& http_client, const std::string& url,
    const std::vector<std::string>& headers) {
  HttpResponse<void> response =
      CF_EXPECT(Probe(http_client, HttpMethod::kHead, url, headers));
  // A pre-signed URL signs the verb, so it refuses the HEAD and takes a GET of
  // one byte instead.
  if (response.http_code == kForbidden ||
      response.http_code == kMethodNotAllowed) {
    std::vector<std::string> ranged = headers;
    ranged.emplace_back("Range: bytes=0-0");
    response =
        CF_EXPECT(Probe(http_client, HttpMethod::kGet, url, std::move(ranged)));
  }

  CF_EXPECTF(!response.HttpRedirect(),
             "'{}' redirects with {}, and redirects are not followed.  Name "
             "the URL it redirects to.",
             ScrubUrl(url), response.http_code);
  CF_EXPECTF(response.HttpSuccess(), "'{}' is missing or inaccessible - {}:{}",
             ScrubUrl(url), response.http_code, response.StatusDescription());

  HttpObjectInfo info = {
      .accept_ranges = ServesRanges(response),
      .size = ProbedSize(response),
  };
  if (std::optional<std::string_view> etag =
          HeaderValue(response.headers, "etag")) {
    info.etag = std::string(*etag);
  }
  return info;
}

}  // namespace cuttlefish

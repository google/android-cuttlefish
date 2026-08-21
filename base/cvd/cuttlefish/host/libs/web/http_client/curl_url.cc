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

#include "cuttlefish/host/libs/web/http_client/curl_url.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "curl/curl.h"
#include "curl/urlapi.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using ManagedCurlUrl = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;

std::optional<std::string> Part(const CURLU* url, CURLUPart part) {
  char* value = nullptr;
  if (curl_url_get(url, part, &value, 0) != CURLUE_OK) {
    return std::nullopt;
  }
  std::string part_value(value);
  curl_free(value);
  return part_value;
}

}  // namespace

Result<UrlParts> SplitUrl(std::string_view url) {
  ManagedCurlUrl handle(curl_url(), curl_url_cleanup);
  CF_EXPECT_NE(handle.get(), nullptr);
  const std::string url_string(url);
  const CURLUcode code = curl_url_set(
      handle.get(), CURLUPART_URL, url_string.c_str(),
      CURLU_NON_SUPPORT_SCHEME | CURLU_PATH_AS_IS | CURLU_ALLOW_SPACE);
  CF_EXPECTF(code == CURLUE_OK, "{}", curl_url_strerror(code));

  std::optional<std::string> host = Part(handle.get(), CURLUPART_HOST);
  CF_EXPECT(host.has_value(), "The URL has no host.");
  return UrlParts{
      .host = std::move(*host),
      .port = Part(handle.get(), CURLUPART_PORT),
      .path = Part(handle.get(), CURLUPART_PATH).value_or(""),
      .query = Part(handle.get(), CURLUPART_QUERY).value_or(""),
  };
}

}  // namespace cuttlefish

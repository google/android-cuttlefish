//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/http_client/http_client.h"

#include <ctype.h>
#include <stddef.h>

#include <optional>
#include <string_view>
#include <vector>

namespace cuttlefish {
namespace {

bool EqualsCaseInsensitive(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); i++) {
    if (tolower(a[i]) != tolower(b[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

HttpClient::~HttpClient() = default;

std::optional<std::string_view> HeaderValue(
    const std::vector<HttpHeader>& headers, std::string_view header_name) {
  for (const HttpHeader& header : headers) {
    if (EqualsCaseInsensitive(header.name, header_name)) {
      return header.value;
    }
  }
  return std::nullopt;
}

}  // namespace cuttlefish

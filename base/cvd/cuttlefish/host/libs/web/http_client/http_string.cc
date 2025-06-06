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

#include "cuttlefish/host/libs/web/http_client/http_string.h"

#include <stdlib.h>

#include <sstream>
#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace {

Result<HttpResponse<std::string>> Download(
    HttpClient& http_client, HttpMethod method, const std::string& url,
    const std::vector<std::string>& headers,
    const std::string& data_to_write = "") {
  std::stringstream stream;
  auto callback = [&stream](char* data, size_t size) -> bool {
    if (data == nullptr) {
      stream = std::stringstream();
      return true;
    }
    stream.write(data, size);
    return true;
  };
  HttpResponse<void> http_response = CF_EXPECT(http_client.DownloadToCallback(
      method, callback, url, headers, data_to_write));
  return HttpResponse<std::string>{stream.str(), http_response.http_code};
}

}  // namespace

Result<HttpResponse<std::string>> HttpGetToString(
    HttpClient& http_client, const std::string& url,
    const std::vector<std::string>& headers) {
  return CF_EXPECT(Download(http_client, HttpMethod::kGet, url, headers));
}

Result<HttpResponse<std::string>> HttpPostToString(
    HttpClient& client, const std::string& url, const std::string& data,
    const std::vector<std::string>& headers) {
  return CF_EXPECT(Download(client, HttpMethod::kPost, url, headers, data));
}

}  // namespace cuttlefish

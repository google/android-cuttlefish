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
#include <utility>
#include <vector>

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<HttpResponse<std::string>> Download(HttpClient& http_client,
                                           HttpRequest request) {
  std::stringstream stream;
  auto callback = [&stream](char* data, size_t size) -> bool {
    if (data == nullptr) {
      stream = std::stringstream();
      return true;
    }
    stream.write(data, size);
    return true;
  };
  HttpResponse<void> http_response =
      CF_EXPECT(http_client.DownloadToCallback(request, callback));
  return HttpResponse<std::string>{
      .data = stream.str(),
      .http_code = http_response.http_code,
      .headers = std::move(http_response.headers),
  };
}

}  // namespace

Result<HttpResponse<std::string>> HttpGetToString(
    HttpClient& http_client, const std::string& url,
    const std::vector<std::string>& headers) {
  HttpRequest request = {
      .method = HttpMethod::kGet,
      .url = url,
      .headers = headers,
  };
  return CF_EXPECT(Download(http_client, request));
}

Result<HttpResponse<std::string>> HttpPostToString(
    HttpClient& http_client, const std::string& url, const std::string& data,
    const std::vector<std::string>& headers) {
  HttpRequest request = {
      .method = HttpMethod::kPost,
      .url = url,
      .headers = headers,
      .data_to_write = data,
  };
  return CF_EXPECT(Download(http_client, request));
}

}  // namespace cuttlefish

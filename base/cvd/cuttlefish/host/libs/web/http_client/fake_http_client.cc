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

#include "cuttlefish/host/libs/web/http_client/fake_http_client.h"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"

namespace cuttlefish {

void FakeHttpClient::SetResponse(std::string data, std::string url) {
  auto handler = [data = std::move(data)](const HttpRequest&) { return data; };
  SetResponse(std::move(handler), std::move(url));
}

void FakeHttpClient::SetResponse(FakeHttpClient::Handler handler,
                                 std::string url) {
  std::lock_guard lock(mutex_);
  responses_[std::move(url)] = std::move(handler);
}

const FakeHttpClient::Handler* FakeHttpClient::FindHandler(
    std::string_view url) const {
  std::string_view best_url;
  const FakeHttpClient::Handler* best_handler = nullptr;
  for (const auto& [response_url, response_handler] : responses_) {
    bool has_url = url.find(response_url) != std::string_view::npos;
    bool url_longer = response_url.size() > best_url.size();
    bool needs_handler = best_handler == nullptr;
    if (has_url && (url_longer || needs_handler)) {
      best_url = response_url;
      best_handler = &response_handler;
    }
  }
  return best_handler;
}

Result<HttpResponse<void>> FakeHttpClient::DownloadToCallback(
    HttpRequest request, HttpClient::DataCallback callback) {
  std::lock_guard lock(mutex_);
  CF_EXPECT(callback(nullptr, 0));
  const Handler* handler = FindHandler(request.url);
  HttpResponse<void> response;
  if (!handler) {
    response.http_code = 404;
    return response;
  }
  response.http_code = 200;
  std::string data = (*handler)(request);
  CF_EXPECT(callback(data.data(), data.size()));
  return response;
}

}  // namespace cuttlefish

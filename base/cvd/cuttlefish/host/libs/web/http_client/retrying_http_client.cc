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

#include "cuttlefish/host/libs/web/http_client/retrying_http_client.h"

#include <stdio.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

std::chrono::seconds GetRetryAfter(const HttpResponse<void>& response,
                                   std::chrono::milliseconds default_value) {
  std::chrono::seconds retry_after =
      std::chrono::duration_cast<std::chrono::seconds>(default_value);
  std::optional<std::string_view> retry_after_str =
      HeaderValue(response.headers, kRetryAfterHeaderName);
  if (retry_after_str) {
    int value;
    if (absl::SimpleAtoi(*retry_after_str, &value)) {
      retry_after = std::chrono::seconds(value);
    }
  }
  return retry_after;
}

class ServerErrorRetryClient : public HttpClient {
 public:
  ServerErrorRetryClient(HttpClient& inner, int retry_attempts,
                         std::chrono::milliseconds retry_delay)
      : inner_client_(inner),
        retry_attempts_(retry_attempts),
        retry_delay_(retry_delay) {}

  Result<HttpResponse<void>> DownloadToCallback(
      HttpRequest request, DataCallback callback) override {
    HttpResponse<void> response;
    for (int attempt = 0; attempt != retry_attempts_; ++attempt) {
      response = CF_EXPECT(inner_client_.DownloadToCallback(request, callback));
      if (response.HttpServerError()) {
        LOG(ERROR) << "HTTP error response, code " << response.http_code;
        std::this_thread::sleep_for(retry_delay_);
      } else if (response.http_code == kTooManyRequestsCode) {
        LOG(ERROR) << "HTTP Too Many Requests response";
        const std::chrono::seconds retry_after =
            GetRetryAfter(response, retry_delay_);
        LOG(ERROR) << "Retry-After (or retry delay): " << retry_after;
        std::this_thread::sleep_for(retry_after);
      } else {
        return response;
      }
    }
    return response;
  }

 private:
  HttpClient& inner_client_;
  int retry_attempts_;
  std::chrono::milliseconds retry_delay_;
};

}  // namespace

std::unique_ptr<HttpClient> RetryingServerErrorHttpClient(
    HttpClient& inner, int retry_attempts,
    std::chrono::milliseconds retry_delay) {
  return std::make_unique<class ServerErrorRetryClient>(inner, retry_attempts,
                                                        retry_delay);
}

}  // namespace cuttlefish

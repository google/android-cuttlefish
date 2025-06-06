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
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <json/value.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace {

class ServerErrorRetryClient : public HttpClient {
 public:
  ServerErrorRetryClient(HttpClient& inner, int retry_attempts,
                         std::chrono::milliseconds retry_delay)
      : inner_client_(inner),
        retry_attempts_(retry_attempts),
        retry_delay_(retry_delay) {}

  Result<HttpResponse<std::string>> GetToString(
      const std::string& url, const std::vector<std::string>& headers) override {
    auto fn = [&, this]() { return inner_client_.GetToString(url, headers); };
    return CF_EXPECT(RetryImpl<std::string>(fn));
  }

  Result<HttpResponse<std::string>> PostToString(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.PostToString(url, data, headers);
    };
    return CF_EXPECT(RetryImpl<std::string>(fn));
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const Json::Value& data,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.PostToJson(url, data, headers);
    };
    return CF_EXPECT(RetryImpl<Json::Value>(fn));
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.PostToJson(url, data, headers);
    };
    return CF_EXPECT(RetryImpl<Json::Value>(fn));
  }

  Result<HttpResponse<std::string>> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.DownloadToFile(url, path, headers);
    };
    return CF_EXPECT(RetryImpl<std::string>(fn));
  }

  Result<HttpResponse<Json::Value>> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.DownloadToJson(url, headers);
    };
    return CF_EXPECT(RetryImpl<Json::Value>(fn));
  }

  Result<HttpResponse<void>> DownloadToCallback(
      HttpMethod method, DataCallback cb, const std::string& url,
      const std::vector<std::string>& hdrs,
      const std::string& to_write) override {
    auto fn = [&, this]() {
      return inner_client_.DownloadToCallback(method, cb, url, hdrs, to_write);
    };
    return CF_EXPECT(RetryImpl<void>(fn));
  }

 private:
  template <typename T>
  Result<HttpResponse<T>> RetryImpl(
      std::function<Result<HttpResponse<T>>()> attempt_fn) {
    HttpResponse<T> response;
    for (int attempt = 0; attempt != retry_attempts_; ++attempt) {
      if (attempt != 0) {
        std::this_thread::sleep_for(retry_delay_);
      }
      response = CF_EXPECT(attempt_fn());
      if (!response.HttpServerError()) {
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

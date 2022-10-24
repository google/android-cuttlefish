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

#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <type_traits>

#include <json/json.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {

static inline bool IsHttpSuccess(int http_code) {
  return http_code >= 200 && http_code <= 299;
};

struct HttpVoidResponse {};

template <typename T>
struct HttpResponse {
  bool HttpInfo() { return http_code >= 100 && http_code <= 199; }
  bool HttpSuccess() { return IsHttpSuccess(http_code); }
  bool HttpRedirect() { return http_code >= 300 && http_code <= 399; }
  bool HttpClientError() { return http_code >= 400 && http_code <= 499; }
  bool HttpServerError() { return http_code >= 500 && http_code <= 599; }

  typename std::conditional<std::is_void_v<T>, HttpVoidResponse, T>::type data;
  long http_code;
};

using NameResolver =
    std::function<Result<std::vector<std::string>>(const std::string&)>;

Result<std::vector<std::string>> GetEntDnsResolve(const std::string& host);

class HttpClient {
 public:
  typedef std::function<bool(char*, size_t)> DataCallback;

  static std::unique_ptr<HttpClient> CurlClient(
      NameResolver resolver = NameResolver());
  static std::unique_ptr<HttpClient> ServerErrorRetryClient(
      HttpClient&, int retry_attempts, std::chrono::milliseconds retry_delay);

  virtual ~HttpClient();

  virtual Result<HttpResponse<std::string>> GetToString(
      const std::string& url, const std::vector<std::string>& headers = {}) = 0;
  virtual Result<HttpResponse<std::string>> PostToString(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers = {}) = 0;
  virtual Result<HttpResponse<std::string>> DeleteToString(
      const std::string& url, const std::vector<std::string>& headers = {}) = 0;

  // Returns the json object contained in the response's body.
  //
  // NOTE: In case of a parsing error a successful `result` will be returned
  // with the relevant http status code and a json object with the next format:
  // {
  //   "error": "Failed to parse json",
  //   "response: "<THE RESPONSE BODY>"
  // }
  virtual Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers = {}) = 0;
  virtual Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const Json::Value& data,
      const std::vector<std::string>& headers = {}) = 0;
  virtual Result<HttpResponse<Json::Value>> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers = {}) = 0;
  virtual Result<HttpResponse<Json::Value>> DeleteToJson(
      const std::string& url, const std::vector<std::string>& headers = {}) = 0;

  virtual Result<HttpResponse<std::string>> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers = {}) = 0;

  // Returns response's status code.
  virtual Result<HttpResponse<void>> DownloadToCallback(
      DataCallback callback, const std::string& url,
      const std::vector<std::string>& headers = {}) = 0;

  virtual std::string UrlEscape(const std::string&) = 0;
};

}  // namespace cuttlefish

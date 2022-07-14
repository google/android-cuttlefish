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

#include <json/json.h>

namespace cuttlefish {

template <typename T>
struct HttpResponse {
  bool HttpInfo() { return http_code >= 100 && http_code <= 199; }
  bool HttpSuccess() { return http_code >= 200 && http_code <= 299; }
  bool HttpRedirect() { return http_code >= 300 && http_code <= 399; }
  bool HttpClientError() { return http_code >= 400 && http_code <= 499; }
  bool HttpServerError() { return http_code >= 500 && http_code <= 599; }

  T data;
  long http_code;
};

class HttpClient {
 public:
  typedef std::function<bool(char*, size_t)> DataCallback;

  static std::unique_ptr<HttpClient> CurlClient();
  static std::unique_ptr<HttpClient> ServerErrorRetryClient(
      HttpClient&, int retry_attempts, std::chrono::milliseconds retry_delay);
  virtual ~HttpClient();

  virtual HttpResponse<std::string> PostToString(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers = {}) = 0;
  virtual HttpResponse<Json::Value> PostToJson(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers = {}) = 0;
  virtual HttpResponse<Json::Value> PostToJson(
      const std::string& url, const Json::Value& data,
      const std::vector<std::string>& headers = {}) = 0;

  virtual HttpResponse<std::string> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers = {}) = 0;
  virtual HttpResponse<std::string> DownloadToString(
      const std::string& url, const std::vector<std::string>& headers = {}) = 0;
  virtual HttpResponse<Json::Value> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers = {}) = 0;
  virtual HttpResponse<bool> DownloadToCallback(
      DataCallback callback, const std::string& url,
      const std::vector<std::string>& headers = {}) = 0;

  virtual HttpResponse<Json::Value> DeleteToJson(
      const std::string& url, const std::vector<std::string>& headers = {}) = 0;

  virtual std::string UrlEscape(const std::string&) = 0;
};

}  // namespace cuttlefish

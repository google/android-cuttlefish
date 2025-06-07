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

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include <fmt/format.h>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

struct HttpVoidResponse {};

template <typename T>
struct HttpResponse {
  bool HttpInfo() const { return http_code >= 100 && http_code <= 199; }
  bool HttpSuccess() const { return http_code >= 200 && http_code <= 299; }
  bool HttpRedirect() const { return http_code >= 300 && http_code <= 399; }
  bool HttpClientError() const { return http_code >= 400 && http_code <= 499; }
  bool HttpServerError() const { return http_code >= 500 && http_code <= 599; }

  std::string StatusDescription() const {
    switch (http_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "File Not Found";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return fmt::format("Status Code: {}", http_code);
    }
  }

  typename std::conditional<std::is_void_v<T>, HttpVoidResponse, T>::type data;
  long http_code;
};

enum class HttpMethod {
  kGet,
  kPost,
  kDelete,
};

struct HttpRequest {
  HttpMethod method;
  std::string url;
  std::vector<std::string> headers;
  std::string data_to_write;
};

class HttpClient {
 public:
  typedef std::function<bool(char*, size_t)> DataCallback;

  virtual ~HttpClient();

  // Returns response's status code.
  virtual Result<HttpResponse<void>> DownloadToCallback(
      HttpRequest, DataCallback callback) = 0;
};

}  // namespace cuttlefish

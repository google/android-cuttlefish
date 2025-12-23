//
// Copyright (C) 2025 The Android Open Source Project
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
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class FakeHttpClient : public HttpClient {
 public:
  using Handler = std::function<HttpResponse<std::string>(const HttpRequest&)>;

  // The longest string that is a complete substring of `url` is used to match
  // requests.
  void SetResponse(std::string data, std::string url = "");
  void SetResponse(Handler handler, std::string url = "");
  // Returns response's status code.
  Result<HttpResponse<void>> DownloadToCallback(
      HttpRequest request, HttpClient::DataCallback callback) override;

 private:
  const Handler* FindHandler(std::string_view url) const;

  std::mutex mutex_;
  std::unordered_map<std::string, Handler> responses_;
};

}  // namespace cuttlefish

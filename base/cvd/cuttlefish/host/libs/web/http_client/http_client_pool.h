//
// Copyright (C) 2026 The Android Open Source Project
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

#include <stddef.h>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stack>

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class HttpClientPool : public HttpClient {
 public:
  using CreateClient = std::function<Result<std::unique_ptr<HttpClient>>()>;

  // max_clients = 0 implies an unlimited number of clients internally.
  explicit HttpClientPool(CreateClient create, size_t max_clients = 0);

  Result<HttpResponse<void>> DownloadToCallback(
      HttpRequest, ResultDataCallback callback) override;

 private:
  const CreateClient create_;
  const size_t max_clients_;
  std::condition_variable condition_variable_;
  std::mutex mutex_;
  std::stack<std::unique_ptr<HttpClient>> free_clients_;
  size_t clients_in_use_;
};

}  // namespace cuttlefish

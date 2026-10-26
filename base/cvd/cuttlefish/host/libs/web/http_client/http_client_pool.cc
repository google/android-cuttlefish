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

#include "cuttlefish/host/libs/web/http_client/http_client_pool.h"

#include <stddef.h>

#include <memory>
#include <mutex>
#include <utility>

#include "absl/log/check.h"

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

HttpClientPool::HttpClientPool(HttpClientPool::CreateClient create,
                               const size_t max_clients)
    : create_(std::move(create)),
      max_clients_(max_clients),
      clients_in_use_(0) {
  CHECK(create_);
}

// Clients are either held in reserve in `free_clients_`, or exist in the
// `client` local variable in this function. To avoid creating an excess number
// of clients, `max_clients_` tracks how many can be active at once.
Result<HttpResponse<void>> HttpClientPool::DownloadToCallback(
    HttpRequest request, HttpClient::ResultDataCallback callback) {
  std::unique_ptr<HttpClient> client;
  for (std::unique_lock lock(mutex_); client == nullptr;) {
    if (!free_clients_.empty()) {
      client = std::move(free_clients_.top());
      free_clients_.pop();
      clients_in_use_++;
      break;
    } else if (max_clients_ == 0 || clients_in_use_ < max_clients_) {
      client = CF_EXPECT(create_());
      CF_EXPECT(client != nullptr);
      clients_in_use_++;
      break;
    } else {
      condition_variable_.wait(lock);
    }
  }
  Result<HttpResponse<void>> res =
      client->DownloadToCallback(std::move(request), std::move(callback));
  {
    const std::lock_guard lock(mutex_);
    free_clients_.push(std::move(client));
    clients_in_use_--;
  }
  condition_variable_.notify_one();
  return CF_EXPECT(std::move(res));
}

}  // namespace cuttlefish

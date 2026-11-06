//
// Copyright (C) 2024 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"

#include <stddef.h>

#include <mutex>

#include "curl/curl.h"

namespace cuttlefish {

static size_t curl_initialized_count = 0;
static std::mutex* const initialize_curl_mutex = new std::mutex;

CurlGlobalInit::CurlGlobalInit() {
  std::lock_guard lock(*initialize_curl_mutex);
  if (curl_initialized_count == 0) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
  }
  curl_initialized_count++;
}

CurlGlobalInit::CurlGlobalInit(const CurlGlobalInit&) : CurlGlobalInit() {}
CurlGlobalInit::CurlGlobalInit(CurlGlobalInit&&) : CurlGlobalInit() {}

CurlGlobalInit::~CurlGlobalInit() {
  std::lock_guard lock(*initialize_curl_mutex);
  curl_initialized_count--;
  if (curl_initialized_count == 0) {
    curl_global_cleanup();
  }
}

}  // namespace cuttlefish

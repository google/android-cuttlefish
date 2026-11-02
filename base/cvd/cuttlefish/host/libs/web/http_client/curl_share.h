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

#include <array>
#include <mutex>

#include "curl/curl.h"

#include "cuttlefish/host/libs/web/http_client/curl_global_init.h"

#if defined(__clang__) && (!defined(SWIG)) && \
    (!defined(THREAD_ANNOTATION_ATTRIBUTE__))
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)  // no-op
#endif

namespace cuttlefish {

// https://curl.se/libcurl/c/libcurl-share.html
class THREAD_ANNOTATION_ATTRIBUTE__(capability("mutex")) CurlShare {
 public:
  CurlShare();
  CurlShare(const CurlShare&) = delete;
  ~CurlShare();
  CurlShare& operator=(const CurlShare&) = delete;

  CURLSH* Raw();

 private:
  static void LockCallback(CURL*, curl_lock_data, curl_lock_access,
                           void* userdata)
      THREAD_ANNOTATION_ATTRIBUTE__(
          acquire_capability(reinterpret_cast<CurlShare*>(userdata)));
  static void UnlockCallback(CURL*, curl_lock_data, void* userdata)
      THREAD_ANNOTATION_ATTRIBUTE__(
          release_capability(reinterpret_cast<CurlShare*>(userdata)));

  void Lock(curl_lock_data) THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability());
  void Unlock(curl_lock_data)
      THREAD_ANNOTATION_ATTRIBUTE__(release_capability());

  const CurlGlobalInit ensure_init_;
  std::array<std::mutex, CURL_LOCK_DATA_LAST> mutexes_;
  CURLSH* const share_;
};

}  // namespace cuttlefish

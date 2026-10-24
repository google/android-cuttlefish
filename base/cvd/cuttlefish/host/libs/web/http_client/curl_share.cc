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

#include "cuttlefish/host/libs/web/http_client/curl_share.h"

#include "absl/log/check.h"
#include "curl/curl.h"

namespace cuttlefish {

CurlShare::CurlShare() : share_(curl_share_init()) {
  if (!share_) {
    return;
  }
  curl_share_setopt(share_, CURLSHOPT_USERDATA, this);
  // static_cast to verify callback signature
  curl_share_setopt(share_, CURLSHOPT_LOCKFUNC,
                    static_cast<curl_lock_function>(CurlShare::LockCallback));
  curl_share_setopt(
      share_, CURLSHOPT_UNLOCKFUNC,
      static_cast<curl_unlock_function>(CurlShare::UnlockCallback));

  curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
  curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_PSL);
  curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
}

CurlShare::~CurlShare() {
  if (share_ != nullptr) {
    CHECK_EQ(curl_share_cleanup(share_), CURLSHE_OK);
  }
}

CURLSH* CurlShare::Raw() { return share_; }

void CurlShare::LockCallback(CURL* const, const curl_lock_data data,
                             const curl_lock_access, void* const userdata) {
  CurlShare* const share = reinterpret_cast<CurlShare*>(userdata);
  CHECK_LT(data, share->mutexes_.size());
  // The curl_lock_access argument tells us if we need to lock it for
  // reading or writing, but we don't get this information at unlock time,
  // which is inconvenient for managing shared_mutex state.
  share->mutexes_[data].lock();
}

void CurlShare::UnlockCallback(CURL* const, const curl_lock_data data,
                               void* const userdata) {
  CurlShare* const share = reinterpret_cast<CurlShare*>(userdata);
  CHECK_LT(data, share->mutexes_.size());
  share->mutexes_[data].unlock();
}

}  // namespace cuttlefish

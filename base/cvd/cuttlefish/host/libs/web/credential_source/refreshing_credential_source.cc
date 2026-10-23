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

#include "cuttlefish/host/libs/web/credential_source/refreshing_credential_source.h"

#include <chrono>
#include <mutex>
#include <string>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

constexpr auto kRefreshWindow = std::chrono::minutes(2);

}  // namespace

RefreshingCredentialSource::RefreshingCredentialSource()
    : expiration_(std::chrono::steady_clock::time_point::min()) {}

Result<std::string> RefreshingCredentialSource::Credential() {
  std::lock_guard lock(latest_credential_mutex_);
  if (expiration_ + kRefreshWindow < std::chrono::steady_clock::now()) {
    const auto& [credential, expiration] = CF_EXPECT(Refresh());
    latest_credential_ = credential;
    expiration_ = std::chrono::steady_clock::now() + expiration;
  }
  return latest_credential_;
}

}  // namespace cuttlefish

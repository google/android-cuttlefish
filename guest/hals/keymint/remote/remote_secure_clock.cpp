/*
 * Copyright 2020, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.security.secureclock-impl.remote"
#include <log/log.h>

#include "guest/hals/keymint/remote/remote_secure_clock.h"

#include <aidl/android/hardware/security/keymint/ErrorCode.h>

#include <keymaster/android_keymaster.h>
#include <keymaster/keymaster_configuration.h>
#include "KeyMintUtils.h"

namespace aidl::android::hardware::security::secureclock {

using namespace ::keymaster;
using namespace ::aidl::android::hardware::security::keymint::km_utils;

RemoteSecureClock::RemoteSecureClock(keymaster::RemoteKeymaster& impl)
    : impl_(impl) {}

RemoteSecureClock::~RemoteSecureClock() {}

ScopedAStatus RemoteSecureClock::generateTimeStamp(int64_t challenge,
                                                   TimeStampToken* token) {
  GenerateTimestampTokenRequest request(impl_.message_version());
  request.challenge = challenge;
  GenerateTimestampTokenResponse response(request.message_version);
  impl_.GenerateTimestampToken(request, &response);
  if (response.error != KM_ERROR_OK) {
    return kmError2ScopedAStatus(response.error);
  }
  token->challenge = response.token.challenge;
  token->timestamp.milliSeconds =
      static_cast<int64_t>(response.token.timestamp);
  token->mac = kmBlob2vector(response.token.mac);
  return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::security::secureclock

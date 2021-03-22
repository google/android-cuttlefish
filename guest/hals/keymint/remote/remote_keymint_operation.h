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

#pragma once

#include <aidl/android/hardware/security/keymint/BnKeyMintOperation.h>
#include <aidl/android/hardware/security/secureclock/ISecureClock.h>

#include <hardware/keymaster_defs.h>

#include "guest/hals/keymint/remote/remote_keymaster.h"

namespace keymaster {
class AndroidKeymaster;
}

namespace aidl::android::hardware::security::keymint {

using ::ndk::ScopedAStatus;
using secureclock::TimeStampToken;
using std::optional;
using std::shared_ptr;
using std::string;
using std::vector;

class RemoteKeyMintOperation : public BnKeyMintOperation {
 public:
  explicit RemoteKeyMintOperation(::keymaster::RemoteKeymaster& implementation,
                                  keymaster_operation_handle_t opHandle);
  virtual ~RemoteKeyMintOperation();

  ScopedAStatus updateAad(
      const vector<uint8_t>& input,
      const optional<HardwareAuthToken>& authToken,
      const optional<TimeStampToken>& timestampToken) override;

  ScopedAStatus update(const vector<uint8_t>& input,
                       const optional<HardwareAuthToken>& authToken,
                       const optional<TimeStampToken>& timestampToken,
                       vector<uint8_t>* output) override;

  ScopedAStatus finish(const optional<vector<uint8_t>>& input,        //
                       const optional<vector<uint8_t>>& signature,    //
                       const optional<HardwareAuthToken>& authToken,  //
                       const optional<TimeStampToken>& timestampToken,
                       const optional<vector<uint8_t>>& confirmationToken,
                       vector<uint8_t>* output) override;

  ScopedAStatus abort() override;

 protected:
  ::keymaster::RemoteKeymaster& impl_;
  keymaster_operation_handle_t opHandle_;
};

}  // namespace aidl::android::hardware::security::keymint

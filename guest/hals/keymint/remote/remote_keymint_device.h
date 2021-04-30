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

#include <aidl/android/hardware/security/keymint/BnKeyMintDevice.h>
#include <aidl/android/hardware/security/keymint/BnKeyMintOperation.h>
#include <aidl/android/hardware/security/keymint/HardwareAuthToken.h>

#include "guest/hals/keymint/remote/remote_keymaster.h"

namespace aidl::android::hardware::security::keymint {
using ::ndk::ScopedAStatus;
using std::optional;
using std::shared_ptr;
using std::vector;

using secureclock::TimeStampToken;

class RemoteKeyMintDevice : public BnKeyMintDevice {
 public:
  explicit RemoteKeyMintDevice(::keymaster::RemoteKeymaster&, SecurityLevel);
  virtual ~RemoteKeyMintDevice();

  ScopedAStatus getHardwareInfo(KeyMintHardwareInfo* info) override;

  ScopedAStatus addRngEntropy(const vector<uint8_t>& data) override;

  ScopedAStatus generateKey(const vector<KeyParameter>& keyParams,
                            const optional<AttestationKey>& attestationKey,
                            KeyCreationResult* creationResult) override;

  ScopedAStatus importKey(const vector<KeyParameter>& keyParams,
                          KeyFormat keyFormat, const vector<uint8_t>& keyData,
                          const optional<AttestationKey>& attestationKey,
                          KeyCreationResult* creationResult) override;

  ScopedAStatus importWrappedKey(const vector<uint8_t>& wrappedKeyData,
                                 const vector<uint8_t>& wrappingKeyBlob,
                                 const vector<uint8_t>& maskingKey,
                                 const vector<KeyParameter>& unwrappingParams,
                                 int64_t passwordSid, int64_t biometricSid,
                                 KeyCreationResult* creationResult) override;

  ScopedAStatus upgradeKey(const vector<uint8_t>& keyBlobToUpgrade,
                           const vector<KeyParameter>& upgradeParams,
                           vector<uint8_t>* keyBlob) override;

  ScopedAStatus deleteKey(const vector<uint8_t>& keyBlob) override;
  ScopedAStatus deleteAllKeys() override;
  ScopedAStatus destroyAttestationIds() override;

  ScopedAStatus begin(KeyPurpose purpose, const vector<uint8_t>& keyBlob,
                      const vector<KeyParameter>& params,
                      const optional<HardwareAuthToken>& authToken,
                      BeginResult* result) override;

  ScopedAStatus deviceLocked(
      bool passwordOnly,
      const optional<TimeStampToken>& timestampToken) override;
  ScopedAStatus earlyBootEnded() override;

  ScopedAStatus convertStorageKeyToEphemeral(
      const std::vector<uint8_t>& storageKeyBlob,
      std::vector<uint8_t>* ephemeralKeyBlob) override;

  ScopedAStatus getKeyCharacteristics(
      const std::vector<uint8_t>& storageKeyBlob,
      const std::vector<uint8_t>& appId, const std::vector<uint8_t>& appData,
      std::vector<KeyCharacteristics>* keyCharacteristics) override;

 protected:
  ::keymaster::RemoteKeymaster& impl_;
  SecurityLevel securityLevel_;
};

}  // namespace aidl::android::hardware::security::keymint

/*
 * Copyright 2021, The Android Open Source Project
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

#include <cstdint>
#include <vector>

#include <aidl/android/hardware/security/keymint/BnRemotelyProvisionedComponent.h>
#include <aidl/android/hardware/security/keymint/MacedPublicKey.h>
#include <aidl/android/hardware/security/keymint/RpcHardwareInfo.h>
#include <aidl/android/hardware/security/keymint/SecurityLevel.h>

#include "KeyMintUtils.h"
#include "guest/hals/keymint/remote/remote_keymaster.h"

namespace aidl::android::hardware::security::keymint {

class RemoteRemotelyProvisionedComponent
    : public BnRemotelyProvisionedComponent {
 public:
  explicit RemoteRemotelyProvisionedComponent(keymaster::RemoteKeymaster& impl);

  ndk::ScopedAStatus getHardwareInfo(RpcHardwareInfo* info) override;

  ndk::ScopedAStatus generateEcdsaP256KeyPair(
      bool testMode, MacedPublicKey* macedPublicKey,
      std::vector<uint8_t>* privateKeyHandle) override;

  ndk::ScopedAStatus generateCertificateRequest(
      bool testMode, const std::vector<MacedPublicKey>& keysToSign,
      const std::vector<uint8_t>& endpointEncCertChain,
      const std::vector<uint8_t>& challenge, DeviceInfo* deviceInfo,
      ProtectedData* protectedData,
      std::vector<uint8_t>* keysToSignMac) override;

  ndk::ScopedAStatus generateCertificateRequestV2(
      const std::vector<MacedPublicKey>& keysToSign,
      const std::vector<uint8_t>& challenge,
      std::vector<uint8_t>* csr) override;

 private:
  keymaster::RemoteKeymaster& impl_;
};

}  // namespace aidl::android::hardware::security::keymint

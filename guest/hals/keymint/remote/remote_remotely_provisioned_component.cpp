/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <aidl/android/hardware/security/keymint/RpcHardwareInfo.h>
#include <cppbor.h>
#include <cppbor_parse.h>
#include <keymaster/cppcose/cppcose.h>
#include <keymaster/keymaster_configuration.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <variant>

#include "KeyMintUtils.h"
#include "android/binder_auto_utils.h"
#include "remote_remotely_provisioned_component.h"

namespace aidl::android::hardware::security::keymint {
namespace {
using namespace cppcose;
using namespace keymaster;

using ::aidl::android::hardware::security::keymint::km_utils::kmBlob2vector;
using ::aidl::android::hardware::security::keymint::km_utils::
    kmError2ScopedAStatus;
using ::ndk::ScopedAStatus;

// Error codes from the provisioning stack are negated.
ndk::ScopedAStatus toKeymasterError(const KeymasterResponse& response) {
  auto error =
      static_cast<keymaster_error_t>(-static_cast<int32_t>(response.error));
  return kmError2ScopedAStatus(error);
}

}  // namespace

RemoteRemotelyProvisionedComponent::RemoteRemotelyProvisionedComponent(
    keymaster::RemoteKeymaster& impl)
    : impl_(impl) {}

ScopedAStatus RemoteRemotelyProvisionedComponent::getHardwareInfo(
    RpcHardwareInfo* info) {
  GetHwInfoResponse response = impl_.GetHwInfo();
  if (response.error != KM_ERROR_OK) {
    return toKeymasterError(response);
  }

  info->versionNumber = response.version;
  info->rpcAuthorName = response.rpcAuthorName;
  info->supportedEekCurve = response.supportedEekCurve;
  info->uniqueId = response.uniqueId;
  info->supportedNumKeysInCsr = response.supportedNumKeysInCsr;
  return ScopedAStatus::ok();
}

ScopedAStatus RemoteRemotelyProvisionedComponent::generateEcdsaP256KeyPair(
    bool testMode, MacedPublicKey* macedPublicKey,
    std::vector<uint8_t>* privateKeyHandle) {
  GenerateRkpKeyRequest request(impl_.message_version());
  request.test_mode = testMode;
  GenerateRkpKeyResponse response(impl_.message_version());
  impl_.GenerateRkpKey(request, &response);
  if (response.error != KM_ERROR_OK) {
    return toKeymasterError(response);
  }

  macedPublicKey->macedKey = km_utils::kmBlob2vector(response.maced_public_key);
  *privateKeyHandle = km_utils::kmBlob2vector(response.key_blob);
  return ScopedAStatus::ok();
}

ScopedAStatus RemoteRemotelyProvisionedComponent::generateCertificateRequest(
    bool testMode, const std::vector<MacedPublicKey>& keysToSign,
    const std::vector<uint8_t>& endpointEncCertChain,
    const std::vector<uint8_t>& challenge, DeviceInfo* deviceInfo,
    ProtectedData* protectedData, std::vector<uint8_t>* keysToSignMac) {
  GenerateCsrRequest request(impl_.message_version());
  request.test_mode = testMode;
  request.num_keys = keysToSign.size();
  request.keys_to_sign_array = new KeymasterBlob[keysToSign.size()];
  for (size_t i = 0; i < keysToSign.size(); i++) {
    request.SetKeyToSign(i, keysToSign[i].macedKey.data(),
                         keysToSign[i].macedKey.size());
  }
  request.SetEndpointEncCertChain(endpointEncCertChain.data(),
                                  endpointEncCertChain.size());
  request.SetChallenge(challenge.data(), challenge.size());
  GenerateCsrResponse response(impl_.message_version());
  impl_.GenerateCsr(request, &response);

  if (response.error != KM_ERROR_OK) {
    return toKeymasterError(response);
  }
  deviceInfo->deviceInfo = km_utils::kmBlob2vector(response.device_info_blob);
  protectedData->protectedData =
      km_utils::kmBlob2vector(response.protected_data_blob);
  *keysToSignMac = km_utils::kmBlob2vector(response.keys_to_sign_mac);
  return ScopedAStatus::ok();
}

ScopedAStatus RemoteRemotelyProvisionedComponent::generateCertificateRequestV2(
    const std::vector<MacedPublicKey>& keysToSign,
    const std::vector<uint8_t>& challenge, std::vector<uint8_t>* csr) {
  GenerateCsrV2Request request(impl_.message_version());
  if (!request.InitKeysToSign(keysToSign.size())) {
    return kmError2ScopedAStatus(static_cast<keymaster_error_t>(
        BnRemotelyProvisionedComponent::STATUS_FAILED));
  }

  for (size_t i = 0; i < keysToSign.size(); i++) {
    request.SetKeyToSign(i, keysToSign[i].macedKey.data(),
                         keysToSign[i].macedKey.size());
  }
  request.SetChallenge(challenge.data(), challenge.size());
  GenerateCsrV2Response response(impl_.message_version());
  impl_.GenerateCsrV2(request, &response);

  if (response.error != KM_ERROR_OK) {
    return toKeymasterError(response);
  }
  *csr = km_utils::kmBlob2vector(response.csr);
  return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::security::keymint

/*
 * Copyright 2019, The Android Open Source Project
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

#define LOG_TAG "IdentityCredentialStore"

#include <android-base/logging.h>

#include "IdentityCredential.h"
#include "IdentityCredentialStore.h"
#include "WritableIdentityCredential.h"

namespace aidl::android::hardware::identity {

using ::aidl::android::hardware::security::keymint::
    IRemotelyProvisionedComponent;

ndk::ScopedAStatus IdentityCredentialStore::getHardwareInformation(
    HardwareInformation* hardwareInformation) {
  HardwareInformation hw;
  hw.credentialStoreName =
      "Identity Credential Cuttlefish Remote Implementation";
  hw.credentialStoreAuthorName = "Google";
  hw.dataChunkSize = kGcmChunkSize;
  hw.isDirectAccess = false;
  hw.supportedDocTypes = {};
  *hardwareInformation = hw;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredentialStore::createCredential(
    const string& docType, bool testCredential,
    shared_ptr<IWritableIdentityCredential>* outWritableCredential) {
  sp<SecureHardwareProvisioningProxy> hwProxy =
      hwProxyFactory_->createProvisioningProxy();
  shared_ptr<WritableIdentityCredential> wc =
      ndk::SharedRefBase::make<WritableIdentityCredential>(hwProxy, docType,
                                                           testCredential);
  if (!wc->initialize()) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error initializing WritableIdentityCredential"));
  }
  *outWritableCredential = wc;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredentialStore::getCredential(
    CipherSuite cipherSuite, const vector<uint8_t>& credentialData,
    shared_ptr<IIdentityCredential>* outCredential) {
  // We only support CIPHERSUITE_ECDHE_HKDF_ECDSA_WITH_AES_256_GCM_SHA256 right
  // now.
  if (cipherSuite !=
      CipherSuite::CIPHERSUITE_ECDHE_HKDF_ECDSA_WITH_AES_256_GCM_SHA256) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_CIPHER_SUITE_NOT_SUPPORTED,
        "Unsupported cipher suite"));
  }

  sp<SecureHardwarePresentationProxy> hwProxy =
      hwProxyFactory_->createPresentationProxy();
  shared_ptr<IdentityCredential> credential =
      ndk::SharedRefBase::make<IdentityCredential>(hwProxyFactory_, hwProxy,
                                                   credentialData);
  auto ret = credential->initialize();
  if (ret != IIdentityCredentialStore::STATUS_OK) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        int(ret), "Error initializing IdentityCredential"));
  }
  *outCredential = credential;
  return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::identity

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

#ifndef ANDROID_HARDWARE_IDENTITY_WRITABLEIDENTITYCREDENTIAL_H
#define ANDROID_HARDWARE_IDENTITY_WRITABLEIDENTITYCREDENTIAL_H

#include <aidl/android/hardware/identity/BnWritableIdentityCredential.h>
#include <android/hardware/identity/support/IdentityCredentialSupport.h>

#include <cppbor.h>
#include <set>

#include "IdentityCredentialStore.h"
#include "SecureHardwareProxy.h"

namespace aidl::android::hardware::identity {

using ::android::sp;
using ::android::hardware::identity::SecureHardwareProvisioningProxy;
using ::std::set;
using ::std::string;
using ::std::vector;

class WritableIdentityCredential : public BnWritableIdentityCredential {
 public:
  // For a new credential, call initialize() right after construction.
  //
  // For an updated credential, call initializeForUpdate() right after
  // construction.
  //
  WritableIdentityCredential(sp<SecureHardwareProvisioningProxy> hwProxy,
                             const string& docType, bool testCredential)
      : hwProxy_(hwProxy), docType_(docType), testCredential_(testCredential) {}

  ~WritableIdentityCredential();

  // Creates the Credential Key. Returns false on failure.
  bool initialize();

  // Used when updating a credential. Returns false on failure.
  bool initializeForUpdate(const vector<uint8_t>& encryptedCredentialKeys);

  // Methods from IWritableIdentityCredential follow.
  ndk::ScopedAStatus getAttestationCertificate(
      const vector<uint8_t>& attestationApplicationId,
      const vector<uint8_t>& attestationChallenge,
      vector<Certificate>* outCertificateChain) override;

  ndk::ScopedAStatus setExpectedProofOfProvisioningSize(
      int32_t expectedProofOfProvisioningSize) override;

  ndk::ScopedAStatus startPersonalization(
      int32_t accessControlProfileCount,
      const vector<int32_t>& entryCounts) override;

  ndk::ScopedAStatus addAccessControlProfile(
      int32_t id, const Certificate& readerCertificate,
      bool userAuthenticationRequired, int64_t timeoutMillis,
      int64_t secureUserId,
      SecureAccessControlProfile* outSecureAccessControlProfile) override;

  ndk::ScopedAStatus beginAddEntry(
      const vector<int32_t>& accessControlProfileIds, const string& nameSpace,
      const string& name, int32_t entrySize) override;
  ndk::ScopedAStatus addEntryValue(
      const vector<uint8_t>& content,
      vector<uint8_t>* outEncryptedContent) override;

  ndk::ScopedAStatus finishAddingEntries(
      vector<uint8_t>* outCredentialData,
      vector<uint8_t>* outProofOfProvisioningSignature) override;

 private:
  // Set by constructor.
  sp<SecureHardwareProvisioningProxy> hwProxy_;
  string docType_;
  bool testCredential_;

  // This is set in initialize().
  bool startPersonalizationCalled_;
  bool firstEntry_;

  // This is set in getAttestationCertificate().
  bool getAttestationCertificateAlreadyCalled_ = false;

  // These fields are initialized during startPersonalization()
  size_t numAccessControlProfileRemaining_;
  vector<int32_t> remainingEntryCounts_;
  cppbor::Array signedDataAccessControlProfiles_;
  cppbor::Map signedDataNamespaces_;
  cppbor::Array signedDataCurrentNamespace_;
  size_t expectedProofOfProvisioningSize_;

  // This field is initialized in addAccessControlProfile
  set<int32_t> accessControlProfileIds_;

  // These fields are initialized during beginAddEntry()
  size_t entryRemainingBytes_;
  string entryNameSpace_;
  string entryName_;
  vector<int32_t> entryAccessControlProfileIds_;
  vector<uint8_t> entryBytes_;
  set<string> allNameSpaces_;
};

}  // namespace aidl::android::hardware::identity

#endif  // ANDROID_HARDWARE_IDENTITY_WRITABLEIDENTITYCREDENTIAL_H

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

#ifndef ANDROID_HARDWARE_IDENTITY_IDENTITYCREDENTIAL_H
#define ANDROID_HARDWARE_IDENTITY_IDENTITYCREDENTIAL_H

#include <aidl/android/hardware/identity/BnIdentityCredential.h>
#include <aidl/android/hardware/keymaster/HardwareAuthToken.h>
#include <aidl/android/hardware/keymaster/VerificationToken.h>
#include <android/hardware/identity/support/IdentityCredentialSupport.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <cppbor.h>

#include "IdentityCredentialStore.h"
#include "SecureHardwareProxy.h"

namespace aidl::android::hardware::identity {

using ::aidl::android::hardware::keymaster::HardwareAuthToken;
using ::aidl::android::hardware::keymaster::VerificationToken;
using ::android::sp;
using ::android::hardware::identity::SecureHardwarePresentationProxy;
using ::std::map;
using ::std::set;
using ::std::string;
using ::std::vector;

class IdentityCredential : public BnIdentityCredential {
 public:
  IdentityCredential(sp<SecureHardwareProxyFactory> hwProxyFactory,
                     sp<SecureHardwarePresentationProxy> hwProxy,
                     const vector<uint8_t>& credentialData)
      : hwProxyFactory_(hwProxyFactory),
        hwProxy_(hwProxy),
        credentialData_(credentialData),
        numStartRetrievalCalls_(0),
        expectedDeviceNameSpacesSize_(0) {}

  // Parses and decrypts credentialData_, return a status code from
  // IIdentityCredentialStore. Must be called right after construction.
  int initialize();

  // Methods from IIdentityCredential follow.
  ndk::ScopedAStatus deleteCredential(
      vector<uint8_t>* outProofOfDeletionSignature) override;
  ndk::ScopedAStatus deleteCredentialWithChallenge(
      const vector<uint8_t>& challenge,
      vector<uint8_t>* outProofOfDeletionSignature) override;
  ndk::ScopedAStatus proveOwnership(
      const vector<uint8_t>& challenge,
      vector<uint8_t>* outProofOfOwnershipSignature) override;
  ndk::ScopedAStatus createEphemeralKeyPair(
      vector<uint8_t>* outKeyPair) override;
  ndk::ScopedAStatus setReaderEphemeralPublicKey(
      const vector<uint8_t>& publicKey) override;
  ndk::ScopedAStatus createAuthChallenge(int64_t* outChallenge) override;
  ndk::ScopedAStatus setRequestedNamespaces(
      const vector<RequestNamespace>& requestNamespaces) override;
  ndk::ScopedAStatus setVerificationToken(
      const VerificationToken& verificationToken) override;
  ndk::ScopedAStatus startRetrieval(
      const vector<SecureAccessControlProfile>& accessControlProfiles,
      const HardwareAuthToken& authToken, const vector<uint8_t>& itemsRequest,
      const vector<uint8_t>& signingKeyBlob,
      const vector<uint8_t>& sessionTranscript,
      const vector<uint8_t>& readerSignature,
      const vector<int32_t>& requestCounts) override;
  ndk::ScopedAStatus startRetrieveEntryValue(
      const string& nameSpace, const string& name, int32_t entrySize,
      const vector<int32_t>& accessControlProfileIds) override;
  ndk::ScopedAStatus retrieveEntryValue(const vector<uint8_t>& encryptedContent,
                                        vector<uint8_t>* outContent) override;
  ndk::ScopedAStatus finishRetrieval(
      vector<uint8_t>* outMac, vector<uint8_t>* outDeviceNameSpaces) override;
  ndk::ScopedAStatus generateSigningKeyPair(
      vector<uint8_t>* outSigningKeyBlob,
      Certificate* outSigningKeyCertificate) override;

  ndk::ScopedAStatus updateCredential(
      shared_ptr<IWritableIdentityCredential>* outWritableCredential) override;

 private:
  ndk::ScopedAStatus deleteCredentialCommon(
      const vector<uint8_t>& challenge, bool includeChallenge,
      vector<uint8_t>* outProofOfDeletionSignature);

  // Set by constructor
  sp<SecureHardwareProxyFactory> hwProxyFactory_;
  sp<SecureHardwarePresentationProxy> hwProxy_;
  vector<uint8_t> credentialData_;
  int numStartRetrievalCalls_;

  // Set by initialize()
  string docType_;
  bool testCredential_;
  vector<uint8_t> encryptedCredentialKeys_;

  // Set by createEphemeralKeyPair()
  vector<uint8_t> ephemeralPublicKey_;

  // Set by setReaderEphemeralPublicKey()
  vector<uint8_t> readerPublicKey_;

  // Set by setRequestedNamespaces()
  vector<RequestNamespace> requestNamespaces_;

  // Set by setVerificationToken().
  VerificationToken verificationToken_;

  // Set at startRetrieval() time.
  vector<uint8_t> signingKeyBlob_;
  vector<uint8_t> sessionTranscript_;
  vector<uint8_t> itemsRequest_;
  vector<int32_t> requestCountsRemaining_;
  map<string, set<string>> requestedNameSpacesAndNames_;
  cppbor::Map deviceNameSpacesMap_;
  cppbor::Map currentNameSpaceDeviceNameSpacesMap_;

  // Calculated at startRetrieval() time.
  size_t expectedDeviceNameSpacesSize_;
  vector<unsigned int> expectedNumEntriesPerNamespace_;

  // Set at startRetrieveEntryValue() time.
  string currentNameSpace_;
  string currentName_;
  vector<int32_t> currentAccessControlProfileIds_;
  size_t entryRemainingBytes_;
  vector<uint8_t> entryValue_;

  void calcDeviceNameSpacesSize(uint32_t accessControlProfileMask);
};

}  // namespace aidl::android::hardware::identity

#endif  // ANDROID_HARDWARE_IDENTITY_IDENTITYCREDENTIAL_H

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

#ifndef ANDROID_HARDWARE_IDENTITY_SECUREHARDWAREPROXY_H
#define ANDROID_HARDWARE_IDENTITY_SECUREHARDWAREPROXY_H

#include <utils/RefBase.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace android::hardware::identity {

using ::android::RefBase;
using ::std::optional;
using ::std::pair;
using ::std::string;
using ::std::vector;

// These classes are used to communicate with Secure Hardware. They mimic the
// API in libEmbeddedIC 1:1 (except for using C++ types) as each call is
// intended to be forwarded to the Secure Hardware.
//
// Instances are instantiated when a provisioning or presentation session
// starts. When the session is complete, the shutdown() method is called.
//

// Forward declare.
//
class SecureHardwareProvisioningProxy;
class SecureHardwarePresentationProxy;

// This is a class used to create proxies.
//
class SecureHardwareProxyFactory : public RefBase {
 public:
  SecureHardwareProxyFactory() {}
  virtual ~SecureHardwareProxyFactory() {}

  virtual sp<SecureHardwareProvisioningProxy> createProvisioningProxy() = 0;
  virtual sp<SecureHardwarePresentationProxy> createPresentationProxy() = 0;
};

// The proxy used for provisioning.
//
class SecureHardwareProvisioningProxy : public RefBase {
 public:
  SecureHardwareProvisioningProxy() {}
  virtual ~SecureHardwareProvisioningProxy() {}

  virtual bool initialize(bool testCredential) = 0;

  virtual bool initializeForUpdate(bool testCredential, string docType,
                                   vector<uint8_t> encryptedCredentialKeys) = 0;

  // Returns public key certificate chain with attestation.
  //
  // This must return an entire certificate chain and its implementation must
  // be coordinated with the implementation of eicOpsCreateCredentialKey() on
  // the TA side (which may return just a single certificate or the entire
  // chain).
  virtual optional<vector<uint8_t>> createCredentialKey(
      const vector<uint8_t>& challenge,
      const vector<uint8_t>& applicationId) = 0;

  virtual bool startPersonalization(int accessControlProfileCount,
                                    vector<int> entryCounts,
                                    const string& docType,
                                    size_t expectedProofOfProvisioningSize) = 0;

  // Returns MAC (28 bytes).
  virtual optional<vector<uint8_t>> addAccessControlProfile(
      int id, const vector<uint8_t>& readerCertificate,
      bool userAuthenticationRequired, uint64_t timeoutMillis,
      uint64_t secureUserId) = 0;

  virtual bool beginAddEntry(const vector<int>& accessControlProfileIds,
                             const string& nameSpace, const string& name,
                             uint64_t entrySize) = 0;

  // Returns encryptedContent.
  virtual optional<vector<uint8_t>> addEntryValue(
      const vector<int>& accessControlProfileIds, const string& nameSpace,
      const string& name, const vector<uint8_t>& content) = 0;

  // Returns signatureOfToBeSigned (EIC_ECDSA_P256_SIGNATURE_SIZE bytes).
  virtual optional<vector<uint8_t>> finishAddingEntries() = 0;

  // Returns encryptedCredentialKeys (80 bytes).
  virtual optional<vector<uint8_t>> finishGetCredentialData(
      const string& docType) = 0;

  virtual bool shutdown() = 0;
};

enum AccessCheckResult {
  kOk,
  kFailed,
  kNoAccessControlProfiles,
  kUserAuthenticationFailed,
  kReaderAuthenticationFailed,
};

// The proxy used for presentation.
//
class SecureHardwarePresentationProxy : public RefBase {
 public:
  SecureHardwarePresentationProxy() {}
  virtual ~SecureHardwarePresentationProxy() {}

  virtual bool initialize(bool testCredential, string docType,
                          vector<uint8_t> encryptedCredentialKeys) = 0;

  // Returns publicKeyCert (1st component) and signingKeyBlob (2nd component)
  virtual optional<pair<vector<uint8_t>, vector<uint8_t>>>
  generateSigningKeyPair(string docType, time_t now) = 0;

  // Returns private key
  virtual optional<vector<uint8_t>> createEphemeralKeyPair() = 0;

  virtual optional<uint64_t> createAuthChallenge() = 0;

  virtual bool startRetrieveEntries() = 0;

  virtual bool setAuthToken(uint64_t challenge, uint64_t secureUserId,
                            uint64_t authenticatorId,
                            int hardwareAuthenticatorType, uint64_t timeStamp,
                            const vector<uint8_t>& mac,
                            uint64_t verificationTokenChallenge,
                            uint64_t verificationTokenTimestamp,
                            int verificationTokenSecurityLevel,
                            const vector<uint8_t>& verificationTokenMac) = 0;

  virtual bool pushReaderCert(const vector<uint8_t>& certX509) = 0;

  virtual optional<bool> validateAccessControlProfile(
      int id, const vector<uint8_t>& readerCertificate,
      bool userAuthenticationRequired, int timeoutMillis, uint64_t secureUserId,
      const vector<uint8_t>& mac) = 0;

  virtual bool validateRequestMessage(
      const vector<uint8_t>& sessionTranscript,
      const vector<uint8_t>& requestMessage, int coseSignAlg,
      const vector<uint8_t>& readerSignatureOfToBeSigned) = 0;

  virtual bool calcMacKey(const vector<uint8_t>& sessionTranscript,
                          const vector<uint8_t>& readerEphemeralPublicKey,
                          const vector<uint8_t>& signingKeyBlob,
                          const string& docType,
                          unsigned int numNamespacesWithValues,
                          size_t expectedProofOfProvisioningSize) = 0;

  virtual AccessCheckResult startRetrieveEntryValue(
      const string& nameSpace, const string& name,
      unsigned int newNamespaceNumEntries, int32_t entrySize,
      const vector<int32_t>& accessControlProfileIds) = 0;

  virtual optional<vector<uint8_t>> retrieveEntryValue(
      const vector<uint8_t>& encryptedContent, const string& nameSpace,
      const string& name, const vector<int32_t>& accessControlProfileIds) = 0;

  virtual optional<vector<uint8_t>> finishRetrieval();

  virtual optional<vector<uint8_t>> deleteCredential(
      const string& docType, const vector<uint8_t>& challenge,
      bool includeChallenge, size_t proofOfDeletionCborSize) = 0;

  virtual optional<vector<uint8_t>> proveOwnership(
      const string& docType, bool testCredential,
      const vector<uint8_t>& challenge, size_t proofOfOwnershipCborSize) = 0;

  virtual bool shutdown() = 0;
};

}  // namespace android::hardware::identity

#endif  // ANDROID_HARDWARE_IDENTITY_SECUREHARDWAREPROXY_H

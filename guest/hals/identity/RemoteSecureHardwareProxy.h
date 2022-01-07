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

#ifndef ANDROID_HARDWARE_IDENTITY_FAKESECUREHARDWAREPROXY_H
#define ANDROID_HARDWARE_IDENTITY_FAKESECUREHARDWAREPROXY_H

#include <libeic.h>

#include "SecureHardwareProxy.h"

namespace android::hardware::identity {

// This implementation uses libEmbeddedIC in-process.
//
class RemoteSecureHardwareProvisioningProxy
    : public SecureHardwareProvisioningProxy {
 public:
  RemoteSecureHardwareProvisioningProxy();
  virtual ~RemoteSecureHardwareProvisioningProxy();

  bool initialize(bool testCredential) override;

  bool initializeForUpdate(bool testCredential, string docType,
                           vector<uint8_t> encryptedCredentialKeys) override;

  bool shutdown() override;

  // Returns public key certificate.
  optional<vector<uint8_t>> createCredentialKey(
      const vector<uint8_t>& challenge,
      const vector<uint8_t>& applicationId) override;

  bool startPersonalization(int accessControlProfileCount,
                            vector<int> entryCounts, const string& docType,
                            size_t expectedProofOfProvisioningSize) override;

  // Returns MAC (28 bytes).
  optional<vector<uint8_t>> addAccessControlProfile(
      int id, const vector<uint8_t>& readerCertificate,
      bool userAuthenticationRequired, uint64_t timeoutMillis,
      uint64_t secureUserId) override;

  bool beginAddEntry(const vector<int>& accessControlProfileIds,
                     const string& nameSpace, const string& name,
                     uint64_t entrySize) override;

  // Returns encryptedContent.
  optional<vector<uint8_t>> addEntryValue(
      const vector<int>& accessControlProfileIds, const string& nameSpace,
      const string& name, const vector<uint8_t>& content) override;

  // Returns signatureOfToBeSigned (EIC_ECDSA_P256_SIGNATURE_SIZE bytes).
  optional<vector<uint8_t>> finishAddingEntries() override;

  // Returns encryptedCredentialKeys (80 bytes).
  optional<vector<uint8_t>> finishGetCredentialData(
      const string& docType) override;

 protected:
  EicProvisioning ctx_;
};

// This implementation uses libEmbeddedIC in-process.
//
class RemoteSecureHardwarePresentationProxy
    : public SecureHardwarePresentationProxy {
 public:
  RemoteSecureHardwarePresentationProxy();
  virtual ~RemoteSecureHardwarePresentationProxy();

  bool initialize(bool testCredential, string docType,
                  vector<uint8_t> encryptedCredentialKeys) override;

  // Returns publicKeyCert (1st component) and signingKeyBlob (2nd component)
  optional<pair<vector<uint8_t>, vector<uint8_t>>> generateSigningKeyPair(
      string docType, time_t now) override;

  // Returns private key
  optional<vector<uint8_t>> createEphemeralKeyPair() override;

  optional<uint64_t> createAuthChallenge() override;

  bool startRetrieveEntries() override;

  bool setAuthToken(uint64_t challenge, uint64_t secureUserId,
                    uint64_t authenticatorId, int hardwareAuthenticatorType,
                    uint64_t timeStamp, const vector<uint8_t>& mac,
                    uint64_t verificationTokenChallenge,
                    uint64_t verificationTokenTimestamp,
                    int verificationTokenSecurityLevel,
                    const vector<uint8_t>& verificationTokenMac) override;

  bool pushReaderCert(const vector<uint8_t>& certX509) override;

  optional<bool> validateAccessControlProfile(
      int id, const vector<uint8_t>& readerCertificate,
      bool userAuthenticationRequired, int timeoutMillis, uint64_t secureUserId,
      const vector<uint8_t>& mac) override;

  bool validateRequestMessage(
      const vector<uint8_t>& sessionTranscript,
      const vector<uint8_t>& requestMessage, int coseSignAlg,
      const vector<uint8_t>& readerSignatureOfToBeSigned) override;

  bool calcMacKey(const vector<uint8_t>& sessionTranscript,
                  const vector<uint8_t>& readerEphemeralPublicKey,
                  const vector<uint8_t>& signingKeyBlob, const string& docType,
                  unsigned int numNamespacesWithValues,
                  size_t expectedProofOfProvisioningSize) override;

  AccessCheckResult startRetrieveEntryValue(
      const string& nameSpace, const string& name,
      unsigned int newNamespaceNumEntries, int32_t entrySize,
      const vector<int32_t>& accessControlProfileIds) override;

  optional<vector<uint8_t>> retrieveEntryValue(
      const vector<uint8_t>& encryptedContent, const string& nameSpace,
      const string& name,
      const vector<int32_t>& accessControlProfileIds) override;

  optional<vector<uint8_t>> finishRetrieval() override;

  optional<vector<uint8_t>> deleteCredential(
      const string& docType, const vector<uint8_t>& challenge,
      bool includeChallenge, size_t proofOfDeletionCborSize) override;

  optional<vector<uint8_t>> proveOwnership(
      const string& docType, bool testCredential,
      const vector<uint8_t>& challenge,
      size_t proofOfOwnershipCborSize) override;

  bool shutdown() override;

 protected:
  EicPresentation ctx_;
};

// Factory implementation.
//
class RemoteSecureHardwareProxyFactory : public SecureHardwareProxyFactory {
 public:
  RemoteSecureHardwareProxyFactory() {}
  virtual ~RemoteSecureHardwareProxyFactory() {}

  sp<SecureHardwareProvisioningProxy> createProvisioningProxy() override {
    return new RemoteSecureHardwareProvisioningProxy();
  }

  sp<SecureHardwarePresentationProxy> createPresentationProxy() override {
    return new RemoteSecureHardwarePresentationProxy();
  }
};

}  // namespace android::hardware::identity

#endif  // ANDROID_HARDWARE_IDENTITY_FAKESECUREHARDWAREPROXY_H

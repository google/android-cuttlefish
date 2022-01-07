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

#define LOG_TAG "RemoteSecureHardwareProxy"

#include "RemoteSecureHardwareProxy.h"

#include <android/hardware/identity/support/IdentityCredentialSupport.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <string.h>

#include <openssl/sha.h>

#include <openssl/aes.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hkdf.h>
#include <openssl/hmac.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <libeic.h>

using ::std::optional;
using ::std::string;
using ::std::tuple;
using ::std::vector;

namespace android::hardware::identity {

// ----------------------------------------------------------------------

RemoteSecureHardwareProvisioningProxy::RemoteSecureHardwareProvisioningProxy() {
}

RemoteSecureHardwareProvisioningProxy::
    ~RemoteSecureHardwareProvisioningProxy() {}

bool RemoteSecureHardwareProvisioningProxy::shutdown() {
  LOG(INFO) << "RemoteSecureHardwarePresentationProxy shutdown";
  return true;
}

bool RemoteSecureHardwareProvisioningProxy::initialize(bool testCredential) {
  LOG(INFO) << "RemoteSecureHardwareProvisioningProxy created, "
               "sizeof(EicProvisioning): "
            << sizeof(EicProvisioning);
  return eicProvisioningInit(&ctx_, testCredential);
}

bool RemoteSecureHardwareProvisioningProxy::initializeForUpdate(
    bool testCredential, string docType,
    vector<uint8_t> encryptedCredentialKeys) {
  return eicProvisioningInitForUpdate(
      &ctx_, testCredential, docType.c_str(), docType.size(),
      encryptedCredentialKeys.data(), encryptedCredentialKeys.size());
}

// Returns public key certificate.
optional<vector<uint8_t>>
RemoteSecureHardwareProvisioningProxy::createCredentialKey(
    const vector<uint8_t>& challenge, const vector<uint8_t>& applicationId) {
  uint8_t publicKeyCert[4096];
  size_t publicKeyCertSize = sizeof publicKeyCert;
  if (!eicProvisioningCreateCredentialKey(
          &ctx_, challenge.data(), challenge.size(), applicationId.data(),
          applicationId.size(), publicKeyCert, &publicKeyCertSize)) {
    return {};
  }
  vector<uint8_t> pubKeyCert(publicKeyCertSize);
  memcpy(pubKeyCert.data(), publicKeyCert, publicKeyCertSize);
  return pubKeyCert;
}

bool RemoteSecureHardwareProvisioningProxy::startPersonalization(
    int accessControlProfileCount, vector<int> entryCounts,
    const string& docType, size_t expectedProofOfProvisioningSize) {
  if (!eicProvisioningStartPersonalization(
          &ctx_, accessControlProfileCount, entryCounts.data(),
          entryCounts.size(), docType.c_str(), docType.size(),
          expectedProofOfProvisioningSize)) {
    return false;
  }
  return true;
}

// Returns MAC (28 bytes).
optional<vector<uint8_t>>
RemoteSecureHardwareProvisioningProxy::addAccessControlProfile(
    int id, const vector<uint8_t>& readerCertificate,
    bool userAuthenticationRequired, uint64_t timeoutMillis,
    uint64_t secureUserId) {
  vector<uint8_t> mac(28);
  uint8_t scratchSpace[512];
  if (!eicProvisioningAddAccessControlProfile(
          &ctx_, id, readerCertificate.data(), readerCertificate.size(),
          userAuthenticationRequired, timeoutMillis, secureUserId, mac.data(),
          scratchSpace, sizeof(scratchSpace))) {
    return {};
  }
  return mac;
}

bool RemoteSecureHardwareProvisioningProxy::beginAddEntry(
    const vector<int>& accessControlProfileIds, const string& nameSpace,
    const string& name, uint64_t entrySize) {
  uint8_t scratchSpace[512];
  vector<uint8_t> uint8AccessControlProfileIds;
  for (size_t i = 0; i < accessControlProfileIds.size(); i++) {
    uint8AccessControlProfileIds.push_back(accessControlProfileIds[i] & 0xFF);
  }

  return eicProvisioningBeginAddEntry(
      &ctx_, uint8AccessControlProfileIds.data(),
      uint8AccessControlProfileIds.size(), nameSpace.c_str(), nameSpace.size(),
      name.c_str(), name.size(), entrySize, scratchSpace, sizeof(scratchSpace));
}

// Returns encryptedContent.
optional<vector<uint8_t>> RemoteSecureHardwareProvisioningProxy::addEntryValue(
    const vector<int>& accessControlProfileIds, const string& nameSpace,
    const string& name, const vector<uint8_t>& content) {
  vector<uint8_t> eicEncryptedContent;
  uint8_t scratchSpace[512];
  vector<uint8_t> uint8AccessControlProfileIds;
  for (size_t i = 0; i < accessControlProfileIds.size(); i++) {
    uint8AccessControlProfileIds.push_back(accessControlProfileIds[i] & 0xFF);
  }

  eicEncryptedContent.resize(content.size() + 28);
  if (!eicProvisioningAddEntryValue(&ctx_, uint8AccessControlProfileIds.data(),
                                    uint8AccessControlProfileIds.size(),
                                    nameSpace.c_str(), nameSpace.size(),
                                    name.c_str(), name.size(), content.data(),
                                    content.size(), eicEncryptedContent.data(),
                                    scratchSpace, sizeof(scratchSpace))) {
    return {};
  }
  return eicEncryptedContent;
}

// Returns signatureOfToBeSigned (EIC_ECDSA_P256_SIGNATURE_SIZE bytes).
optional<vector<uint8_t>>
RemoteSecureHardwareProvisioningProxy::finishAddingEntries() {
  vector<uint8_t> signatureOfToBeSigned(EIC_ECDSA_P256_SIGNATURE_SIZE);
  if (!eicProvisioningFinishAddingEntries(&ctx_,
                                          signatureOfToBeSigned.data())) {
    return {};
  }
  return signatureOfToBeSigned;
}

// Returns encryptedCredentialKeys.
optional<vector<uint8_t>>
RemoteSecureHardwareProvisioningProxy::finishGetCredentialData(
    const string& docType) {
  vector<uint8_t> encryptedCredentialKeys(116);
  size_t size = encryptedCredentialKeys.size();
  if (!eicProvisioningFinishGetCredentialData(
          &ctx_, docType.c_str(), docType.size(),
          encryptedCredentialKeys.data(), &size)) {
    return {};
  }
  encryptedCredentialKeys.resize(size);
  return encryptedCredentialKeys;
}

// ----------------------------------------------------------------------

RemoteSecureHardwarePresentationProxy::RemoteSecureHardwarePresentationProxy() {
}

RemoteSecureHardwarePresentationProxy::
    ~RemoteSecureHardwarePresentationProxy() {}

bool RemoteSecureHardwarePresentationProxy::initialize(
    bool testCredential, string docType,
    vector<uint8_t> encryptedCredentialKeys) {
  LOG(INFO) << "RemoteSecureHardwarePresentationProxy created, "
               "sizeof(EicPresentation): "
            << sizeof(EicPresentation);
  return eicPresentationInit(&ctx_, testCredential, docType.c_str(),
                             docType.size(), encryptedCredentialKeys.data(),
                             encryptedCredentialKeys.size());
}

// Returns publicKeyCert (1st component) and signingKeyBlob (2nd component)
optional<pair<vector<uint8_t>, vector<uint8_t>>>
RemoteSecureHardwarePresentationProxy::generateSigningKeyPair(string docType,
                                                              time_t now) {
  uint8_t publicKeyCert[512];
  size_t publicKeyCertSize = sizeof(publicKeyCert);
  vector<uint8_t> signingKeyBlob(60);

  if (!eicPresentationGenerateSigningKeyPair(
          &ctx_, docType.c_str(), docType.size(), now, publicKeyCert,
          &publicKeyCertSize, signingKeyBlob.data())) {
    return {};
  }

  vector<uint8_t> cert;
  cert.resize(publicKeyCertSize);
  memcpy(cert.data(), publicKeyCert, publicKeyCertSize);

  return std::make_pair(cert, signingKeyBlob);
}

// Returns private key
optional<vector<uint8_t>>
RemoteSecureHardwarePresentationProxy::createEphemeralKeyPair() {
  vector<uint8_t> priv(EIC_P256_PRIV_KEY_SIZE);
  if (!eicPresentationCreateEphemeralKeyPair(&ctx_, priv.data())) {
    return {};
  }
  return priv;
}

optional<uint64_t>
RemoteSecureHardwarePresentationProxy::createAuthChallenge() {
  uint64_t challenge;
  if (!eicPresentationCreateAuthChallenge(&ctx_, &challenge)) {
    return {};
  }
  return challenge;
}

bool RemoteSecureHardwarePresentationProxy::shutdown() {
  LOG(INFO) << "RemoteSecureHardwarePresentationProxy shutdown";
  return true;
}

bool RemoteSecureHardwarePresentationProxy::pushReaderCert(
    const vector<uint8_t>& certX509) {
  return eicPresentationPushReaderCert(&ctx_, certX509.data(), certX509.size());
}

bool RemoteSecureHardwarePresentationProxy::validateRequestMessage(
    const vector<uint8_t>& sessionTranscript,
    const vector<uint8_t>& requestMessage, int coseSignAlg,
    const vector<uint8_t>& readerSignatureOfToBeSigned) {
  return eicPresentationValidateRequestMessage(
      &ctx_, sessionTranscript.data(), sessionTranscript.size(),
      requestMessage.data(), requestMessage.size(), coseSignAlg,
      readerSignatureOfToBeSigned.data(), readerSignatureOfToBeSigned.size());
}

bool RemoteSecureHardwarePresentationProxy::setAuthToken(
    uint64_t challenge, uint64_t secureUserId, uint64_t authenticatorId,
    int hardwareAuthenticatorType, uint64_t timeStamp,
    const vector<uint8_t>& mac, uint64_t verificationTokenChallenge,
    uint64_t verificationTokenTimestamp, int verificationTokenSecurityLevel,
    const vector<uint8_t>& verificationTokenMac) {
  return eicPresentationSetAuthToken(
      &ctx_, challenge, secureUserId, authenticatorId,
      hardwareAuthenticatorType, timeStamp, mac.data(), mac.size(),
      verificationTokenChallenge, verificationTokenTimestamp,
      verificationTokenSecurityLevel, verificationTokenMac.data(),
      verificationTokenMac.size());
}

optional<bool>
RemoteSecureHardwarePresentationProxy::validateAccessControlProfile(
    int id, const vector<uint8_t>& readerCertificate,
    bool userAuthenticationRequired, int timeoutMillis, uint64_t secureUserId,
    const vector<uint8_t>& mac) {
  bool accessGranted = false;
  uint8_t scratchSpace[512];
  if (!eicPresentationValidateAccessControlProfile(
          &ctx_, id, readerCertificate.data(), readerCertificate.size(),
          userAuthenticationRequired, timeoutMillis, secureUserId, mac.data(),
          &accessGranted, scratchSpace, sizeof(scratchSpace))) {
    return {};
  }
  return accessGranted;
}

bool RemoteSecureHardwarePresentationProxy::startRetrieveEntries() {
  return eicPresentationStartRetrieveEntries(&ctx_);
}

bool RemoteSecureHardwarePresentationProxy::calcMacKey(
    const vector<uint8_t>& sessionTranscript,
    const vector<uint8_t>& readerEphemeralPublicKey,
    const vector<uint8_t>& signingKeyBlob, const string& docType,
    unsigned int numNamespacesWithValues,
    size_t expectedProofOfProvisioningSize) {
  if (signingKeyBlob.size() != 60) {
    eicDebug("Unexpected size %zd of signingKeyBlob, expected 60",
             signingKeyBlob.size());
    return false;
  }
  return eicPresentationCalcMacKey(
      &ctx_, sessionTranscript.data(), sessionTranscript.size(),
      readerEphemeralPublicKey.data(), signingKeyBlob.data(), docType.c_str(),
      docType.size(), numNamespacesWithValues, expectedProofOfProvisioningSize);
}

AccessCheckResult
RemoteSecureHardwarePresentationProxy::startRetrieveEntryValue(
    const string& nameSpace, const string& name,
    unsigned int newNamespaceNumEntries, int32_t entrySize,
    const vector<int32_t>& accessControlProfileIds) {
  uint8_t scratchSpace[512];
  vector<uint8_t> uint8AccessControlProfileIds;
  for (size_t i = 0; i < accessControlProfileIds.size(); i++) {
    uint8AccessControlProfileIds.push_back(accessControlProfileIds[i] & 0xFF);
  }

  EicAccessCheckResult result = eicPresentationStartRetrieveEntryValue(
      &ctx_, nameSpace.c_str(), nameSpace.size(), name.c_str(), name.size(),
      newNamespaceNumEntries, entrySize, uint8AccessControlProfileIds.data(),
      uint8AccessControlProfileIds.size(), scratchSpace, sizeof(scratchSpace));
  switch (result) {
    case EIC_ACCESS_CHECK_RESULT_OK:
      return AccessCheckResult::kOk;
    case EIC_ACCESS_CHECK_RESULT_NO_ACCESS_CONTROL_PROFILES:
      return AccessCheckResult::kNoAccessControlProfiles;
    case EIC_ACCESS_CHECK_RESULT_FAILED:
      return AccessCheckResult::kFailed;
    case EIC_ACCESS_CHECK_RESULT_USER_AUTHENTICATION_FAILED:
      return AccessCheckResult::kUserAuthenticationFailed;
    case EIC_ACCESS_CHECK_RESULT_READER_AUTHENTICATION_FAILED:
      return AccessCheckResult::kReaderAuthenticationFailed;
  }
  eicDebug("Unknown result with code %d, returning kFailed", (int)result);
  return AccessCheckResult::kFailed;
}

optional<vector<uint8_t>>
RemoteSecureHardwarePresentationProxy::retrieveEntryValue(
    const vector<uint8_t>& encryptedContent, const string& nameSpace,
    const string& name, const vector<int32_t>& accessControlProfileIds) {
  uint8_t scratchSpace[512];
  vector<uint8_t> uint8AccessControlProfileIds;
  for (size_t i = 0; i < accessControlProfileIds.size(); i++) {
    uint8AccessControlProfileIds.push_back(accessControlProfileIds[i] & 0xFF);
  }

  vector<uint8_t> content;
  content.resize(encryptedContent.size() - 28);
  if (!eicPresentationRetrieveEntryValue(
          &ctx_, encryptedContent.data(), encryptedContent.size(),
          content.data(), nameSpace.c_str(), nameSpace.size(), name.c_str(),
          name.size(), uint8AccessControlProfileIds.data(),
          uint8AccessControlProfileIds.size(), scratchSpace,
          sizeof(scratchSpace))) {
    return {};
  }
  return content;
}

optional<vector<uint8_t>>
RemoteSecureHardwarePresentationProxy::finishRetrieval() {
  vector<uint8_t> mac(32);
  size_t macSize = 32;
  if (!eicPresentationFinishRetrieval(&ctx_, mac.data(), &macSize)) {
    return {};
  }
  mac.resize(macSize);
  return mac;
}

optional<vector<uint8_t>>
RemoteSecureHardwarePresentationProxy::deleteCredential(
    const string& docType, const vector<uint8_t>& challenge,
    bool includeChallenge, size_t proofOfDeletionCborSize) {
  vector<uint8_t> signatureOfToBeSigned(EIC_ECDSA_P256_SIGNATURE_SIZE);
  if (!eicPresentationDeleteCredential(
          &ctx_, docType.c_str(), docType.size(), challenge.data(),
          challenge.size(), includeChallenge, proofOfDeletionCborSize,
          signatureOfToBeSigned.data())) {
    return {};
  }
  return signatureOfToBeSigned;
}

optional<vector<uint8_t>> RemoteSecureHardwarePresentationProxy::proveOwnership(
    const string& docType, bool testCredential,
    const vector<uint8_t>& challenge, size_t proofOfOwnershipCborSize) {
  vector<uint8_t> signatureOfToBeSigned(EIC_ECDSA_P256_SIGNATURE_SIZE);
  if (!eicPresentationProveOwnership(&ctx_, docType.c_str(), docType.size(),
                                     testCredential, challenge.data(),
                                     challenge.size(), proofOfOwnershipCborSize,
                                     signatureOfToBeSigned.data())) {
    return {};
  }
  return signatureOfToBeSigned;
}

}  // namespace android::hardware::identity

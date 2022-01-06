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

#define LOG_TAG "WritableIdentityCredential"

#include "WritableIdentityCredential.h"

#include <android/hardware/identity/support/IdentityCredentialSupport.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <cppbor.h>
#include <cppbor_parse.h>

#include <utility>

#include "IdentityCredentialStore.h"

#include "SecureHardwareProxy.h"

namespace aidl::android::hardware::identity {

using ::android::base::StringPrintf;
using ::std::optional;
using namespace ::android::hardware::identity;

bool WritableIdentityCredential::initialize() {
  if (!hwProxy_->initialize(testCredential_)) {
    LOG(ERROR) << "hwProxy->initialize() failed";
    return false;
  }
  startPersonalizationCalled_ = false;
  firstEntry_ = true;

  return true;
}

// Used when updating a credential. Returns false on failure.
bool WritableIdentityCredential::initializeForUpdate(
    const vector<uint8_t>& encryptedCredentialKeys) {
  if (!hwProxy_->initializeForUpdate(testCredential_, docType_,
                                     encryptedCredentialKeys)) {
    LOG(ERROR) << "hwProxy->initializeForUpdate() failed";
    return false;
  }
  startPersonalizationCalled_ = false;
  firstEntry_ = true;

  return true;
}

WritableIdentityCredential::~WritableIdentityCredential() {}

ndk::ScopedAStatus WritableIdentityCredential::getAttestationCertificate(
    const vector<uint8_t>& attestationApplicationId,
    const vector<uint8_t>& attestationChallenge,
    vector<Certificate>* outCertificateChain) {
  if (getAttestationCertificateAlreadyCalled_) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error attestation certificate previously generated"));
  }
  getAttestationCertificateAlreadyCalled_ = true;

  if (attestationChallenge.empty()) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Challenge can not be empty"));
  }

  optional<vector<uint8_t>> certChain = hwProxy_->createCredentialKey(
      attestationChallenge, attestationApplicationId);
  if (!certChain) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error generating attestation certificate chain"));
  }

  optional<vector<vector<uint8_t>>> certs =
      support::certificateChainSplit(certChain.value());
  if (!certs) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error splitting chain into separate certificates"));
  }

  *outCertificateChain = vector<Certificate>();
  for (const vector<uint8_t>& cert : certs.value()) {
    Certificate c = Certificate();
    c.encodedCertificate = cert;
    outCertificateChain->push_back(std::move(c));
  }

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus
WritableIdentityCredential::setExpectedProofOfProvisioningSize(
    int32_t expectedProofOfProvisioningSize) {
  expectedProofOfProvisioningSize_ = expectedProofOfProvisioningSize;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus WritableIdentityCredential::startPersonalization(
    int32_t accessControlProfileCount, const vector<int32_t>& entryCounts) {
  if (startPersonalizationCalled_) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "startPersonalization called already"));
  }
  startPersonalizationCalled_ = true;

  numAccessControlProfileRemaining_ = accessControlProfileCount;
  remainingEntryCounts_ = entryCounts;
  entryNameSpace_ = "";

  signedDataAccessControlProfiles_ = cppbor::Array();
  signedDataNamespaces_ = cppbor::Map();
  signedDataCurrentNamespace_ = cppbor::Array();

  if (!hwProxy_->startPersonalization(accessControlProfileCount, entryCounts,
                                      docType_,
                                      expectedProofOfProvisioningSize_)) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "eicStartPersonalization"));
  }

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus WritableIdentityCredential::addAccessControlProfile(
    int32_t id, const Certificate& readerCertificate,
    bool userAuthenticationRequired, int64_t timeoutMillis,
    int64_t secureUserId,
    SecureAccessControlProfile* outSecureAccessControlProfile) {
  if (numAccessControlProfileRemaining_ == 0) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "numAccessControlProfileRemaining_ is 0 and expected non-zero"));
  }

  if (accessControlProfileIds_.find(id) != accessControlProfileIds_.end()) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Access Control Profile id must be unique"));
  }
  accessControlProfileIds_.insert(id);

  if (id < 0 || id >= 32) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Access Control Profile id must be non-negative and less than 32"));
  }

  // Spec requires if |userAuthenticationRequired| is false, then
  // |timeoutMillis| must also be zero.
  if (!userAuthenticationRequired && timeoutMillis != 0) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "userAuthenticationRequired is false but timeout is non-zero"));
  }

  optional<vector<uint8_t>> mac = hwProxy_->addAccessControlProfile(
      id, readerCertificate.encodedCertificate, userAuthenticationRequired,
      timeoutMillis, secureUserId);
  if (!mac) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "eicAddAccessControlProfile"));
  }

  SecureAccessControlProfile profile;
  profile.id = id;
  profile.readerCertificate = readerCertificate;
  profile.userAuthenticationRequired = userAuthenticationRequired;
  profile.timeoutMillis = timeoutMillis;
  profile.secureUserId = secureUserId;
  profile.mac = mac.value();
  cppbor::Map profileMap;
  profileMap.add("id", profile.id);
  if (profile.readerCertificate.encodedCertificate.size() > 0) {
    profileMap.add("readerCertificate",
                   cppbor::Bstr(profile.readerCertificate.encodedCertificate));
  }
  if (profile.userAuthenticationRequired) {
    profileMap.add("userAuthenticationRequired",
                   profile.userAuthenticationRequired);
    profileMap.add("timeoutMillis", profile.timeoutMillis);
  }
  signedDataAccessControlProfiles_.add(std::move(profileMap));

  numAccessControlProfileRemaining_--;

  *outSecureAccessControlProfile = profile;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus WritableIdentityCredential::beginAddEntry(
    const vector<int32_t>& accessControlProfileIds, const string& nameSpace,
    const string& name, int32_t entrySize) {
  if (numAccessControlProfileRemaining_ != 0) {
    LOG(ERROR) << "numAccessControlProfileRemaining_ is "
               << numAccessControlProfileRemaining_ << " and expected zero";
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "numAccessControlProfileRemaining_ is not zero"));
  }

  // Ensure passed-in profile ids reference valid access control profiles
  for (const int32_t id : accessControlProfileIds) {
    if (accessControlProfileIds_.find(id) == accessControlProfileIds_.end()) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "An id in accessControlProfileIds references non-existing ACP"));
    }
  }

  if (remainingEntryCounts_.size() == 0) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "No more namespaces to add to"));
  }

  // Handle initial beginEntry() call.
  if (firstEntry_) {
    firstEntry_ = false;
    entryNameSpace_ = nameSpace;
    allNameSpaces_.insert(nameSpace);
  }

  // If the namespace changed...
  if (nameSpace != entryNameSpace_) {
    if (allNameSpaces_.find(nameSpace) != allNameSpaces_.end()) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Name space cannot be added in interleaving fashion"));
    }

    // Then check that all entries in the previous namespace have been added..
    if (remainingEntryCounts_[0] != 0) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "New namespace but a non-zero number of entries remain to be added"));
    }
    remainingEntryCounts_.erase(remainingEntryCounts_.begin());
    remainingEntryCounts_[0] -= 1;
    allNameSpaces_.insert(nameSpace);

    if (signedDataCurrentNamespace_.size() > 0) {
      signedDataNamespaces_.add(entryNameSpace_,
                                std::move(signedDataCurrentNamespace_));
      signedDataCurrentNamespace_ = cppbor::Array();
    }
  } else {
    // Same namespace...
    if (remainingEntryCounts_[0] == 0) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Same namespace but no entries remain to be added"));
    }
    remainingEntryCounts_[0] -= 1;
  }

  entryRemainingBytes_ = entrySize;
  entryNameSpace_ = nameSpace;
  entryName_ = name;
  entryAccessControlProfileIds_ = accessControlProfileIds;
  entryBytes_.resize(0);
  // LOG(INFO) << "name=" << name << " entrySize=" << entrySize;

  if (!hwProxy_->beginAddEntry(accessControlProfileIds, nameSpace, name,
                               entrySize)) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "eicBeginAddEntry"));
  }

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus WritableIdentityCredential::addEntryValue(
    const vector<uint8_t>& content, vector<uint8_t>* outEncryptedContent) {
  size_t contentSize = content.size();

  if (contentSize > IdentityCredentialStore::kGcmChunkSize) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Passed in chunk of is bigger than kGcmChunkSize"));
  }
  if (contentSize > entryRemainingBytes_) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Passed in chunk is bigger than remaining space"));
  }

  entryBytes_.insert(entryBytes_.end(), content.begin(), content.end());
  entryRemainingBytes_ -= contentSize;
  if (entryRemainingBytes_ > 0) {
    if (contentSize != IdentityCredentialStore::kGcmChunkSize) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Retrieved non-final chunk which isn't kGcmChunkSize"));
    }
  }

  optional<vector<uint8_t>> encryptedContent = hwProxy_->addEntryValue(
      entryAccessControlProfileIds_, entryNameSpace_, entryName_, content);
  if (!encryptedContent) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "eicAddEntryValue"));
  }

  if (entryRemainingBytes_ == 0) {
    // TODO: ideally do do this without parsing the data (but still validate
    // data is valid CBOR).
    auto [item, _, message] = cppbor::parse(entryBytes_);
    if (item == nullptr) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Data is not valid CBOR"));
    }
    cppbor::Map entryMap;
    entryMap.add("name", entryName_);
    entryMap.add("value", std::move(item));
    cppbor::Array profileIdArray;
    for (auto id : entryAccessControlProfileIds_) {
      profileIdArray.add(id);
    }
    entryMap.add("accessControlProfiles", std::move(profileIdArray));
    signedDataCurrentNamespace_.add(std::move(entryMap));
  }

  *outEncryptedContent = encryptedContent.value();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus WritableIdentityCredential::finishAddingEntries(
    vector<uint8_t>* outCredentialData,
    vector<uint8_t>* outProofOfProvisioningSignature) {
  if (numAccessControlProfileRemaining_ != 0) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "numAccessControlProfileRemaining_ is not 0 and expected zero"));
  }

  if (remainingEntryCounts_.size() > 1 || remainingEntryCounts_[0] != 0) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "More entry spaces remain than startPersonalization configured"));
  }

  if (signedDataCurrentNamespace_.size() > 0) {
    signedDataNamespaces_.add(entryNameSpace_,
                              std::move(signedDataCurrentNamespace_));
  }
  cppbor::Array popArray;
  popArray.add("ProofOfProvisioning")
      .add(docType_)
      .add(std::move(signedDataAccessControlProfiles_))
      .add(std::move(signedDataNamespaces_))
      .add(testCredential_);
  vector<uint8_t> encodedCbor = popArray.encode();

  if (encodedCbor.size() != expectedProofOfProvisioningSize_) {
    LOG(ERROR) << "CBOR for proofOfProvisioning is " << encodedCbor.size()
               << " bytes, "
               << "was expecting " << expectedProofOfProvisioningSize_;
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        StringPrintf("Unexpected CBOR size %zd for proofOfProvisioning, was "
                     "expecting %zd",
                     encodedCbor.size(), expectedProofOfProvisioningSize_)
            .c_str()));
  }

  optional<vector<uint8_t>> signatureOfToBeSigned =
      hwProxy_->finishAddingEntries();
  if (!signatureOfToBeSigned) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "eicFinishAddingEntries"));
  }

  optional<vector<uint8_t>> signature =
      support::coseSignEcDsaWithSignature(signatureOfToBeSigned.value(),
                                          encodedCbor,  // data
                                          {});          // certificateChain
  if (!signature) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "Error signing data"));
  }

  optional<vector<uint8_t>> encryptedCredentialKeys =
      hwProxy_->finishGetCredentialData(docType_);
  if (!encryptedCredentialKeys) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error generating encrypted CredentialKeys"));
  }
  cppbor::Array array;
  array.add(docType_);
  array.add(testCredential_);
  array.add(encryptedCredentialKeys.value());
  vector<uint8_t> credentialData = array.encode();

  *outCredentialData = credentialData;
  *outProofOfProvisioningSignature = signature.value();
  hwProxy_->shutdown();

  return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::identity

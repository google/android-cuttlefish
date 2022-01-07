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

#define LOG_TAG "IdentityCredential"

#include "IdentityCredential.h"
#include "IdentityCredentialStore.h"

#include <android/hardware/identity/support/IdentityCredentialSupport.h>

#include <string.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#include <cppbor.h>
#include <cppbor_parse.h>

#include "SecureHardwareProxy.h"
#include "WritableIdentityCredential.h"

namespace aidl::android::hardware::identity {

using ::aidl::android::hardware::keymaster::Timestamp;
using ::android::base::StringPrintf;
using ::std::optional;

using namespace ::android::hardware::identity;

int IdentityCredential::initialize() {
  if (credentialData_.size() == 0) {
    LOG(ERROR) << "CredentialData is empty";
    return IIdentityCredentialStore::STATUS_INVALID_DATA;
  }
  auto [item, _, message] = cppbor::parse(credentialData_);
  if (item == nullptr) {
    LOG(ERROR) << "CredentialData is not valid CBOR: " << message;
    return IIdentityCredentialStore::STATUS_INVALID_DATA;
  }

  const cppbor::Array* arrayItem = item->asArray();
  if (arrayItem == nullptr || arrayItem->size() != 3) {
    LOG(ERROR) << "CredentialData is not an array with three elements";
    return IIdentityCredentialStore::STATUS_INVALID_DATA;
  }

  const cppbor::Tstr* docTypeItem = (*arrayItem)[0]->asTstr();
  const cppbor::Bool* testCredentialItem =
      ((*arrayItem)[1]->asSimple() != nullptr
           ? ((*arrayItem)[1]->asSimple()->asBool())
           : nullptr);
  const cppbor::Bstr* encryptedCredentialKeysItem = (*arrayItem)[2]->asBstr();
  if (docTypeItem == nullptr || testCredentialItem == nullptr ||
      encryptedCredentialKeysItem == nullptr) {
    LOG(ERROR) << "CredentialData unexpected item types";
    return IIdentityCredentialStore::STATUS_INVALID_DATA;
  }

  docType_ = docTypeItem->value();
  testCredential_ = testCredentialItem->value();

  encryptedCredentialKeys_ = encryptedCredentialKeysItem->value();
  if (!hwProxy_->initialize(testCredential_, docType_,
                            encryptedCredentialKeys_)) {
    LOG(ERROR) << "hwProxy->initialize failed";
    return false;
  }

  return IIdentityCredentialStore::STATUS_OK;
}

ndk::ScopedAStatus IdentityCredential::deleteCredential(
    vector<uint8_t>* outProofOfDeletionSignature) {
  return deleteCredentialCommon({}, false, outProofOfDeletionSignature);
}

ndk::ScopedAStatus IdentityCredential::deleteCredentialWithChallenge(
    const vector<uint8_t>& challenge,
    vector<uint8_t>* outProofOfDeletionSignature) {
  return deleteCredentialCommon(challenge, true, outProofOfDeletionSignature);
}

ndk::ScopedAStatus IdentityCredential::deleteCredentialCommon(
    const vector<uint8_t>& challenge, bool includeChallenge,
    vector<uint8_t>* outProofOfDeletionSignature) {
  if (challenge.size() > 32) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA, "Challenge too big"));
  }

  cppbor::Array array = {"ProofOfDeletion", docType_, testCredential_};
  if (includeChallenge) {
    array = {"ProofOfDeletion", docType_, challenge, testCredential_};
  }

  vector<uint8_t> proofOfDeletionCbor = array.encode();
  vector<uint8_t> podDigest = support::sha256(proofOfDeletionCbor);

  optional<vector<uint8_t>> signatureOfToBeSigned = hwProxy_->deleteCredential(
      docType_, challenge, includeChallenge, proofOfDeletionCbor.size());
  if (!signatureOfToBeSigned) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error signing ProofOfDeletion"));
  }

  optional<vector<uint8_t>> signature =
      support::coseSignEcDsaWithSignature(signatureOfToBeSigned.value(),
                                          proofOfDeletionCbor,  // data
                                          {});  // certificateChain
  if (!signature) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "Error signing data"));
  }

  *outProofOfDeletionSignature = signature.value();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::proveOwnership(
    const vector<uint8_t>& challenge,
    vector<uint8_t>* outProofOfOwnershipSignature) {
  if (challenge.size() > 32) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA, "Challenge too big"));
  }

  cppbor::Array array;
  array = {"ProofOfOwnership", docType_, challenge, testCredential_};
  vector<uint8_t> proofOfOwnershipCbor = array.encode();
  vector<uint8_t> podDigest = support::sha256(proofOfOwnershipCbor);

  optional<vector<uint8_t>> signatureOfToBeSigned = hwProxy_->proveOwnership(
      docType_, testCredential_, challenge, proofOfOwnershipCbor.size());
  if (!signatureOfToBeSigned) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error signing ProofOfOwnership"));
  }

  optional<vector<uint8_t>> signature =
      support::coseSignEcDsaWithSignature(signatureOfToBeSigned.value(),
                                          proofOfOwnershipCbor,  // data
                                          {});  // certificateChain
  if (!signature) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "Error signing data"));
  }

  *outProofOfOwnershipSignature = signature.value();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::createEphemeralKeyPair(
    vector<uint8_t>* outKeyPair) {
  optional<vector<uint8_t>> ephemeralPriv = hwProxy_->createEphemeralKeyPair();
  if (!ephemeralPriv) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error creating ephemeral key"));
  }
  optional<vector<uint8_t>> keyPair =
      support::ecPrivateKeyToKeyPair(ephemeralPriv.value());
  if (!keyPair) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error creating ephemeral key-pair"));
  }

  // Stash public key of this key-pair for later check in startRetrieval().
  optional<vector<uint8_t>> publicKey =
      support::ecKeyPairGetPublicKey(keyPair.value());
  if (!publicKey) {
    LOG(ERROR) << "Error getting public part of ephemeral key pair";
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error getting public part of ephemeral key pair"));
  }
  ephemeralPublicKey_ = publicKey.value();

  *outKeyPair = keyPair.value();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::setReaderEphemeralPublicKey(
    const vector<uint8_t>& publicKey) {
  readerPublicKey_ = publicKey;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::createAuthChallenge(
    int64_t* outChallenge) {
  optional<uint64_t> challenge = hwProxy_->createAuthChallenge();
  if (!challenge) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "Error generating challenge"));
  }
  *outChallenge = challenge.value();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::setRequestedNamespaces(
    const vector<RequestNamespace>& requestNamespaces) {
  requestNamespaces_ = requestNamespaces;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::setVerificationToken(
    const VerificationToken& verificationToken) {
  verificationToken_ = verificationToken;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::startRetrieval(
    const vector<SecureAccessControlProfile>& accessControlProfiles,
    const HardwareAuthToken& authToken, const vector<uint8_t>& itemsRequest,
    const vector<uint8_t>& signingKeyBlob,
    const vector<uint8_t>& sessionTranscript,
    const vector<uint8_t>& readerSignature,
    const vector<int32_t>& requestCounts) {
  std::unique_ptr<cppbor::Item> sessionTranscriptItem;
  if (sessionTranscript.size() > 0) {
    auto [item, _, message] = cppbor::parse(sessionTranscript);
    if (item == nullptr) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "SessionTranscript contains invalid CBOR"));
    }
    sessionTranscriptItem = std::move(item);
  }
  if (numStartRetrievalCalls_ > 0) {
    if (sessionTranscript_ != sessionTranscript) {
      LOG(ERROR) << "Session Transcript changed";
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_SESSION_TRANSCRIPT_MISMATCH,
          "Passed-in SessionTranscript doesn't match previously used "
          "SessionTranscript"));
    }
  }
  sessionTranscript_ = sessionTranscript;

  // This resets various state in the TA...
  if (!hwProxy_->startRetrieveEntries()) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error starting retrieving entries"));
  }

  optional<vector<uint8_t>> signatureOfToBeSigned;
  if (readerSignature.size() > 0) {
    signatureOfToBeSigned = support::coseSignGetSignature(readerSignature);
    if (!signatureOfToBeSigned) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_READER_SIGNATURE_CHECK_FAILED,
          "Error extracting signatureOfToBeSigned from COSE_Sign1"));
    }
  }

  // Feed the auth token to secure hardware only if they're valid.
  if (authToken.timestamp.milliSeconds != 0) {
    if (!hwProxy_->setAuthToken(
            authToken.challenge, authToken.userId, authToken.authenticatorId,
            int(authToken.authenticatorType), authToken.timestamp.milliSeconds,
            authToken.mac, verificationToken_.challenge,
            verificationToken_.timestamp.milliSeconds,
            int(verificationToken_.securityLevel), verificationToken_.mac)) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA, "Invalid Auth Token"));
    }
  }

  // We'll be feeding ACPs interleaved with certificates from the reader
  // certificate chain...
  vector<SecureAccessControlProfile> remainingAcps = accessControlProfiles;

  // ... and we'll use those ACPs to build up a 32-bit mask indicating which
  // of the possible 32 ACPs grants access.
  uint32_t accessControlProfileMask = 0;

  // If there is a signature, validate that it was made with the top-most key in
  // the certificate chain embedded in the COSE_Sign1 structure.
  optional<vector<uint8_t>> readerCertificateChain;
  if (readerSignature.size() > 0) {
    readerCertificateChain = support::coseSignGetX5Chain(readerSignature);
    if (!readerCertificateChain) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_READER_SIGNATURE_CHECK_FAILED,
          "Unable to get reader certificate chain from COSE_Sign1"));
    }

    // First, feed all the reader certificates to the secure hardware. We start
    // at the end..
    optional<vector<vector<uint8_t>>> splitCerts =
        support::certificateChainSplit(readerCertificateChain.value());
    if (!splitCerts || splitCerts.value().size() == 0) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_READER_SIGNATURE_CHECK_FAILED,
          "Error splitting certificate chain from COSE_Sign1"));
    }
    for (ssize_t n = splitCerts.value().size() - 1; n >= 0; --n) {
      const vector<uint8_t>& x509Cert = splitCerts.value()[n];
      if (!hwProxy_->pushReaderCert(x509Cert)) {
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
            IIdentityCredentialStore::STATUS_READER_SIGNATURE_CHECK_FAILED,
            StringPrintf("Error validating reader certificate %zd", n)
                .c_str()));
      }

      // If we have ACPs for that particular certificate, send them to the
      // TA right now...
      //
      // Remember in this case certificate equality is done by comparing public
      // keys, not bitwise comparison of the certificates.
      //
      optional<vector<uint8_t>> x509CertPubKey =
          support::certificateChainGetTopMostKey(x509Cert);
      if (!x509CertPubKey) {
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
            IIdentityCredentialStore::STATUS_FAILED,
            StringPrintf("Error getting public key from reader certificate %zd",
                         n)
                .c_str()));
      }
      vector<SecureAccessControlProfile>::iterator it = remainingAcps.begin();
      while (it != remainingAcps.end()) {
        const SecureAccessControlProfile& profile = *it;
        if (profile.readerCertificate.encodedCertificate.size() == 0) {
          ++it;
          continue;
        }
        optional<vector<uint8_t>> profilePubKey =
            support::certificateChainGetTopMostKey(
                profile.readerCertificate.encodedCertificate);
        if (!profilePubKey) {
          return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
              IIdentityCredentialStore::STATUS_FAILED,
              "Error getting public key from profile"));
        }
        if (profilePubKey.value() == x509CertPubKey.value()) {
          optional<bool> res = hwProxy_->validateAccessControlProfile(
              profile.id, profile.readerCertificate.encodedCertificate,
              profile.userAuthenticationRequired, profile.timeoutMillis,
              profile.secureUserId, profile.mac);
          if (!res) {
            return ndk::ScopedAStatus(
                AStatus_fromServiceSpecificErrorWithMessage(
                    IIdentityCredentialStore::STATUS_INVALID_DATA,
                    "Error validating access control profile"));
          }
          if (res.value()) {
            accessControlProfileMask |= (1 << profile.id);
          }
          it = remainingAcps.erase(it);
        } else {
          ++it;
        }
      }
    }

    // ... then pass the request message and have the TA check it's signed by
    // the key in last certificate we pushed.
    if (sessionTranscript.size() > 0 && itemsRequest.size() > 0 &&
        readerSignature.size() > 0) {
      optional<vector<uint8_t>> tbsSignature =
          support::coseSignGetSignature(readerSignature);
      if (!tbsSignature) {
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
            IIdentityCredentialStore::STATUS_READER_SIGNATURE_CHECK_FAILED,
            "Error extracting toBeSigned from COSE_Sign1"));
      }
      optional<int> coseSignAlg = support::coseSignGetAlg(readerSignature);
      if (!coseSignAlg) {
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
            IIdentityCredentialStore::STATUS_READER_SIGNATURE_CHECK_FAILED,
            "Error extracting signature algorithm from COSE_Sign1"));
      }
      if (!hwProxy_->validateRequestMessage(sessionTranscript, itemsRequest,
                                            coseSignAlg.value(),
                                            tbsSignature.value())) {
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
            IIdentityCredentialStore::STATUS_READER_SIGNATURE_CHECK_FAILED,
            "readerMessage is not signed by top-level certificate"));
      }
    }
  }

  // Feed remaining access control profiles...
  for (const SecureAccessControlProfile& profile : remainingAcps) {
    optional<bool> res = hwProxy_->validateAccessControlProfile(
        profile.id, profile.readerCertificate.encodedCertificate,
        profile.userAuthenticationRequired, profile.timeoutMillis,
        profile.secureUserId, profile.mac);
    if (!res) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Error validating access control profile"));
    }
    if (res.value()) {
      accessControlProfileMask |= (1 << profile.id);
    }
  }

  // TODO: move this check to the TA
#if 1
  // To prevent replay-attacks, we check that the public part of the ephemeral
  // key we previously created, is present in the DeviceEngagement part of
  // SessionTranscript as a COSE_Key, in uncompressed form.
  //
  // We do this by just searching for the X and Y coordinates.
  if (sessionTranscript.size() > 0) {
    auto [getXYSuccess, ePubX, ePubY] =
        support::ecPublicKeyGetXandY(ephemeralPublicKey_);
    if (!getXYSuccess) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_EPHEMERAL_PUBLIC_KEY_NOT_FOUND,
          "Error extracting X and Y from ePub"));
    }
    if (sessionTranscript.size() > 0 &&
        !(memmem(sessionTranscript.data(), sessionTranscript.size(),
                 ePubX.data(), ePubX.size()) != nullptr &&
          memmem(sessionTranscript.data(), sessionTranscript.size(),
                 ePubY.data(), ePubY.size()) != nullptr)) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_EPHEMERAL_PUBLIC_KEY_NOT_FOUND,
          "Did not find ephemeral public key's X and Y coordinates in "
          "SessionTranscript (make sure leading zeroes are not used)"));
    }
  }
#endif

  // itemsRequest: If non-empty, contains request data that may be signed by the
  // reader.  The content can be defined in the way appropriate for the
  // credential, but there are three requirements that must be met to work with
  // this HAL:
  if (itemsRequest.size() > 0) {
    // 1. The content must be a CBOR-encoded structure.
    auto [item, _, message] = cppbor::parse(itemsRequest);
    if (item == nullptr) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_ITEMS_REQUEST_MESSAGE,
          "Error decoding CBOR in itemsRequest"));
    }

    // 2. The CBOR structure must be a map.
    const cppbor::Map* map = item->asMap();
    if (map == nullptr) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_ITEMS_REQUEST_MESSAGE,
          "itemsRequest is not a CBOR map"));
    }

    // 3. The map must contain a key "nameSpaces" whose value contains a map, as
    // described in
    //    the example below.
    //
    //   NameSpaces = {
    //     + NameSpace => DataElements ; Requested data elements for each
    //     NameSpace
    //   }
    //
    //   NameSpace = tstr
    //
    //   DataElements = {
    //     + DataElement => IntentToRetain
    //   }
    //
    //   DataElement = tstr
    //   IntentToRetain = bool
    //
    // Here's an example of an |itemsRequest| CBOR value satisfying above
    // requirements 1. through 3.:
    //
    //    {
    //        'docType' : 'org.iso.18013-5.2019',
    //        'nameSpaces' : {
    //            'org.iso.18013-5.2019' : {
    //                'Last name' : false,
    //                'Birth date' : false,
    //                'First name' : false,
    //                'Home address' : true
    //            },
    //            'org.aamva.iso.18013-5.2019' : {
    //                'Real Id' : false
    //            }
    //        }
    //    }
    //
    const cppbor::Map* nsMap = nullptr;
    for (size_t n = 0; n < map->size(); n++) {
      const auto& [keyItem, valueItem] = (*map)[n];
      if (keyItem->type() == cppbor::TSTR &&
          keyItem->asTstr()->value() == "nameSpaces" &&
          valueItem->type() == cppbor::MAP) {
        nsMap = valueItem->asMap();
        break;
      }
    }
    if (nsMap == nullptr) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_ITEMS_REQUEST_MESSAGE,
          "No nameSpaces map in top-most map"));
    }

    for (size_t n = 0; n < nsMap->size(); n++) {
      auto& [nsKeyItem, nsValueItem] = (*nsMap)[n];
      const cppbor::Tstr* nsKey = nsKeyItem->asTstr();
      const cppbor::Map* nsInnerMap = nsValueItem->asMap();
      if (nsKey == nullptr || nsInnerMap == nullptr) {
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
            IIdentityCredentialStore::STATUS_INVALID_ITEMS_REQUEST_MESSAGE,
            "Type mismatch in nameSpaces map"));
      }
      string requestedNamespace = nsKey->value();
      set<string> requestedKeys;
      for (size_t m = 0; m < nsInnerMap->size(); m++) {
        const auto& [innerMapKeyItem, innerMapValueItem] = (*nsInnerMap)[m];
        const cppbor::Tstr* nameItem = innerMapKeyItem->asTstr();
        const cppbor::Simple* simple = innerMapValueItem->asSimple();
        const cppbor::Bool* intentToRetainItem =
            (simple != nullptr) ? simple->asBool() : nullptr;
        if (nameItem == nullptr || intentToRetainItem == nullptr) {
          return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
              IIdentityCredentialStore::STATUS_INVALID_ITEMS_REQUEST_MESSAGE,
              "Type mismatch in value in nameSpaces map"));
        }
        requestedKeys.insert(nameItem->value());
      }
      requestedNameSpacesAndNames_[requestedNamespace] = requestedKeys;
    }
  }

  deviceNameSpacesMap_ = cppbor::Map();
  currentNameSpaceDeviceNameSpacesMap_ = cppbor::Map();

  requestCountsRemaining_ = requestCounts;
  currentNameSpace_ = "";

  itemsRequest_ = itemsRequest;
  signingKeyBlob_ = signingKeyBlob;

  // calculate the size of DeviceNameSpaces. We need to know it ahead of time.
  calcDeviceNameSpacesSize(accessControlProfileMask);

  // Count the number of non-empty namespaces
  size_t numNamespacesWithValues = 0;
  for (size_t n = 0; n < expectedNumEntriesPerNamespace_.size(); n++) {
    if (expectedNumEntriesPerNamespace_[n] > 0) {
      numNamespacesWithValues += 1;
    }
  }

  // Finally, pass info so the HMAC key can be derived and the TA can start
  // creating the DeviceNameSpaces CBOR...
  if (sessionTranscript_.size() > 0 && readerPublicKey_.size() > 0 &&
      signingKeyBlob.size() > 0) {
    // We expect the reader ephemeral public key to be same size and curve
    // as the ephemeral key we generated (e.g. P-256 key), otherwise ECDH
    // won't work. So its length should be 65 bytes and it should be
    // starting with 0x04.
    if (readerPublicKey_.size() != 65 || readerPublicKey_[0] != 0x04) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_FAILED,
          "Reader public key is not in expected format"));
    }
    vector<uint8_t> pubKeyP256(readerPublicKey_.begin() + 1,
                               readerPublicKey_.end());
    if (!hwProxy_->calcMacKey(sessionTranscript_, pubKeyP256, signingKeyBlob,
                              docType_, numNamespacesWithValues,
                              expectedDeviceNameSpacesSize_)) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_FAILED,
          "Error starting retrieving entries"));
    }
  }

  numStartRetrievalCalls_ += 1;
  return ndk::ScopedAStatus::ok();
}

size_t cborNumBytesForLength(size_t length) {
  if (length < 24) {
    return 0;
  } else if (length <= 0xff) {
    return 1;
  } else if (length <= 0xffff) {
    return 2;
  } else if (length <= 0xffffffff) {
    return 4;
  }
  return 8;
}

size_t cborNumBytesForTstr(const string& value) {
  return 1 + cborNumBytesForLength(value.size()) + value.size();
}

void IdentityCredential::calcDeviceNameSpacesSize(
    uint32_t accessControlProfileMask) {
  /*
   * This is how DeviceNameSpaces is defined:
   *
   *        DeviceNameSpaces = {
   *            * NameSpace => DeviceSignedItems
   *        }
   *        DeviceSignedItems = {
   *            + DataItemName => DataItemValue
   *        }
   *
   *        Namespace = tstr
   *        DataItemName = tstr
   *        DataItemValue = any
   *
   * This function will calculate its length using knowledge of how CBOR is
   * encoded.
   */
  size_t ret = 0;
  vector<unsigned int> numEntriesPerNamespace;
  for (const RequestNamespace& rns : requestNamespaces_) {
    vector<RequestDataItem> itemsToInclude;

    for (const RequestDataItem& rdi : rns.items) {
      // If we have a CBOR request message, skip if item isn't in it
      if (itemsRequest_.size() > 0) {
        const auto& it = requestedNameSpacesAndNames_.find(rns.namespaceName);
        if (it == requestedNameSpacesAndNames_.end()) {
          continue;
        }
        const set<string>& dataItemNames = it->second;
        if (dataItemNames.find(rdi.name) == dataItemNames.end()) {
          continue;
        }
      }

      // Access is granted if at least one of the profiles grants access.
      //
      // If an item is configured without any profiles, access is denied.
      //
      bool authorized = false;
      for (auto id : rdi.accessControlProfileIds) {
        if (accessControlProfileMask & (1 << id)) {
          authorized = true;
          break;
        }
      }
      if (!authorized) {
        continue;
      }

      itemsToInclude.push_back(rdi);
    }

    numEntriesPerNamespace.push_back(itemsToInclude.size());

    // If no entries are to be in the namespace, we don't include it in
    // the CBOR...
    if (itemsToInclude.size() == 0) {
      continue;
    }

    // Key: NameSpace
    ret += cborNumBytesForTstr(rns.namespaceName);

    // Value: Open the DeviceSignedItems map
    ret += 1 + cborNumBytesForLength(itemsToInclude.size());

    for (const RequestDataItem& item : itemsToInclude) {
      // Key: DataItemName
      ret += cborNumBytesForTstr(item.name);

      // Value: DataItemValue - entryData.size is the length of serialized CBOR
      // so we use that.
      ret += item.size;
    }
  }

  // Now that we know the number of namespaces with values, we know how many
  // bytes the DeviceNamespaces map in the beginning is going to take up.
  ret += 1 + cborNumBytesForLength(numEntriesPerNamespace.size());

  expectedDeviceNameSpacesSize_ = ret;
  expectedNumEntriesPerNamespace_ = numEntriesPerNamespace;
}

ndk::ScopedAStatus IdentityCredential::startRetrieveEntryValue(
    const string& nameSpace, const string& name, int32_t entrySize,
    const vector<int32_t>& accessControlProfileIds) {
  if (name.empty()) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA, "Name cannot be empty"));
  }
  if (nameSpace.empty()) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Name space cannot be empty"));
  }

  if (requestCountsRemaining_.size() == 0) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "No more name spaces left to go through"));
  }

  bool newNamespace;
  if (currentNameSpace_ == "") {
    // First call.
    currentNameSpace_ = nameSpace;
    newNamespace = true;
  }

  if (nameSpace == currentNameSpace_) {
    // Same namespace.
    if (requestCountsRemaining_[0] == 0) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "No more entries to be retrieved in current name space"));
    }
    requestCountsRemaining_[0] -= 1;
  } else {
    // New namespace.
    if (requestCountsRemaining_[0] != 0) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Moved to new name space but one or more entries need to be "
          "retrieved "
          "in current name space"));
    }
    if (currentNameSpaceDeviceNameSpacesMap_.size() > 0) {
      deviceNameSpacesMap_.add(currentNameSpace_,
                               std::move(currentNameSpaceDeviceNameSpacesMap_));
    }
    currentNameSpaceDeviceNameSpacesMap_ = cppbor::Map();

    requestCountsRemaining_.erase(requestCountsRemaining_.begin());
    currentNameSpace_ = nameSpace;
    newNamespace = true;
  }

  // It's permissible to have an empty itemsRequest... but if non-empty you can
  // only request what was specified in said itemsRequest. Enforce that.
  if (itemsRequest_.size() > 0) {
    const auto& it = requestedNameSpacesAndNames_.find(nameSpace);
    if (it == requestedNameSpacesAndNames_.end()) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_NOT_IN_REQUEST_MESSAGE,
          "Name space was not requested in startRetrieval"));
    }
    const set<string>& dataItemNames = it->second;
    if (dataItemNames.find(name) == dataItemNames.end()) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_NOT_IN_REQUEST_MESSAGE,
          "Data item name in name space was not requested in startRetrieval"));
    }
  }

  unsigned int newNamespaceNumEntries = 0;
  if (newNamespace) {
    if (expectedNumEntriesPerNamespace_.size() == 0) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "No more populated name spaces left to go through"));
    }
    newNamespaceNumEntries = expectedNumEntriesPerNamespace_[0];
    expectedNumEntriesPerNamespace_.erase(
        expectedNumEntriesPerNamespace_.begin());
  }

  // Access control is enforced in the secure hardware.
  //
  // ... except for STATUS_NOT_IN_REQUEST_MESSAGE, that's handled above (TODO:
  // consolidate).
  //
  AccessCheckResult res =
      hwProxy_->startRetrieveEntryValue(nameSpace, name, newNamespaceNumEntries,
                                        entrySize, accessControlProfileIds);
  switch (res) {
    case AccessCheckResult::kOk:
      /* Do nothing. */
      break;
    case AccessCheckResult::kFailed:
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_FAILED,
          "Access control check failed (failed)"));
      break;
    case AccessCheckResult::kNoAccessControlProfiles:
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_NO_ACCESS_CONTROL_PROFILES,
          "Access control check failed (no access control profiles)"));
      break;
    case AccessCheckResult::kUserAuthenticationFailed:
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_USER_AUTHENTICATION_FAILED,
          "Access control check failed (user auth)"));
      break;
    case AccessCheckResult::kReaderAuthenticationFailed:
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_READER_AUTHENTICATION_FAILED,
          "Access control check failed (reader auth)"));
      break;
  }

  currentName_ = name;
  currentAccessControlProfileIds_ = accessControlProfileIds;
  entryRemainingBytes_ = entrySize;
  entryValue_.resize(0);

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::retrieveEntryValue(
    const vector<uint8_t>& encryptedContent, vector<uint8_t>* outContent) {
  optional<vector<uint8_t>> content = hwProxy_->retrieveEntryValue(
      encryptedContent, currentNameSpace_, currentName_,
      currentAccessControlProfileIds_);
  if (!content) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Error decrypting data"));
  }

  size_t chunkSize = content.value().size();

  if (chunkSize > entryRemainingBytes_) {
    LOG(ERROR) << "Retrieved chunk of size " << chunkSize
               << " is bigger than remaining space of size "
               << entryRemainingBytes_;
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        "Retrieved chunk is bigger than remaining space"));
  }

  entryRemainingBytes_ -= chunkSize;
  if (entryRemainingBytes_ > 0) {
    if (chunkSize != IdentityCredentialStore::kGcmChunkSize) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Retrieved non-final chunk of size which isn't kGcmChunkSize"));
    }
  }

  entryValue_.insert(entryValue_.end(), content.value().begin(),
                     content.value().end());

  if (entryRemainingBytes_ == 0) {
    auto [entryValueItem, _, message] = cppbor::parse(entryValue_);
    if (entryValueItem == nullptr) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Retrieved data which is invalid CBOR"));
    }
    currentNameSpaceDeviceNameSpacesMap_.add(currentName_,
                                             std::move(entryValueItem));
  }

  *outContent = content.value();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::finishRetrieval(
    vector<uint8_t>* outMac, vector<uint8_t>* outDeviceNameSpaces) {
  if (currentNameSpaceDeviceNameSpacesMap_.size() > 0) {
    deviceNameSpacesMap_.add(currentNameSpace_,
                             std::move(currentNameSpaceDeviceNameSpacesMap_));
  }
  vector<uint8_t> encodedDeviceNameSpaces = deviceNameSpacesMap_.encode();

  if (encodedDeviceNameSpaces.size() != expectedDeviceNameSpacesSize_) {
    LOG(ERROR) << "encodedDeviceNameSpaces is "
               << encodedDeviceNameSpaces.size() << " bytes, "
               << "was expecting " << expectedDeviceNameSpacesSize_;
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_INVALID_DATA,
        StringPrintf("Unexpected CBOR size %zd for encodedDeviceNameSpaces, "
                     "was expecting %zd",
                     encodedDeviceNameSpaces.size(),
                     expectedDeviceNameSpacesSize_)
            .c_str()));
  }

  // If there's no signing key or no sessionTranscript or no reader ephemeral
  // public key, we return the empty MAC.
  optional<vector<uint8_t>> mac;
  if (signingKeyBlob_.size() > 0 && sessionTranscript_.size() > 0 &&
      readerPublicKey_.size() > 0) {
    optional<vector<uint8_t>> digestToBeMaced = hwProxy_->finishRetrieval();
    if (!digestToBeMaced || digestToBeMaced.value().size() != 32) {
      return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
          IIdentityCredentialStore::STATUS_INVALID_DATA,
          "Error generating digestToBeMaced"));
    }
    // Now construct COSE_Mac0 from the returned MAC...
    mac = support::coseMacWithDigest(digestToBeMaced.value(), {} /* data */);
  }

  *outMac = mac.value_or(vector<uint8_t>({}));
  *outDeviceNameSpaces = encodedDeviceNameSpaces;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::generateSigningKeyPair(
    vector<uint8_t>* outSigningKeyBlob, Certificate* outSigningKeyCertificate) {
  time_t now = time(NULL);
  optional<pair<vector<uint8_t>, vector<uint8_t>>> pair =
      hwProxy_->generateSigningKeyPair(docType_, now);
  if (!pair) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED, "Error creating signingKey"));
  }

  *outSigningKeyCertificate = Certificate();
  outSigningKeyCertificate->encodedCertificate = pair->first;

  *outSigningKeyBlob = pair->second;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus IdentityCredential::updateCredential(
    shared_ptr<IWritableIdentityCredential>* outWritableCredential) {
  sp<SecureHardwareProvisioningProxy> hwProxy =
      hwProxyFactory_->createProvisioningProxy();
  shared_ptr<WritableIdentityCredential> wc =
      ndk::SharedRefBase::make<WritableIdentityCredential>(hwProxy, docType_,
                                                           testCredential_);
  if (!wc->initializeForUpdate(encryptedCredentialKeys_)) {
    return ndk::ScopedAStatus(AStatus_fromServiceSpecificErrorWithMessage(
        IIdentityCredentialStore::STATUS_FAILED,
        "Error initializing WritableIdentityCredential for update"));
  }
  *outWritableCredential = wc;
  return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::identity

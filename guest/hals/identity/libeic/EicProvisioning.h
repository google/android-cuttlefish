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

#ifndef ANDROID_HARDWARE_IDENTITY_EIC_PROVISIONING_H
#define ANDROID_HARDWARE_IDENTITY_EIC_PROVISIONING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "EicCbor.h"

#define EIC_MAX_NUM_NAMESPACES 32
#define EIC_MAX_NUM_ACCESS_CONTROL_PROFILE_IDS 32

typedef struct {
  // Set by eicCreateCredentialKey() OR eicProvisioningInitForUpdate()
  uint8_t credentialPrivateKey[EIC_P256_PRIV_KEY_SIZE];

  int numEntryCounts;
  uint8_t entryCounts[EIC_MAX_NUM_NAMESPACES];

  int curNamespace;
  int curNamespaceNumProcessed;

  size_t curEntrySize;
  size_t curEntryNumBytesReceived;

  // Set by eicProvisioningInit() OR eicProvisioningInitForUpdate()
  uint8_t storageKey[EIC_AES_128_KEY_SIZE];

  size_t expectedCborSizeAtEnd;

  // SHA-256 for AdditionalData, updated for each entry.
  uint8_t additionalDataSha256[EIC_SHA256_DIGEST_SIZE];

  // Digester just for ProofOfProvisioning (without Sig_structure).
  EicSha256Ctx proofOfProvisioningDigester;

  EicCbor cbor;

  bool testCredential;

  // Set to true if this is an update.
  bool isUpdate;
} EicProvisioning;

bool eicProvisioningInit(EicProvisioning* ctx, bool testCredential);

bool eicProvisioningInitForUpdate(EicProvisioning* ctx, bool testCredential,
                                  const char* docType, size_t docTypeLength,
                                  const uint8_t* encryptedCredentialKeys,
                                  size_t encryptedCredentialKeysSize);

bool eicProvisioningCreateCredentialKey(
    EicProvisioning* ctx, const uint8_t* challenge, size_t challengeSize,
    const uint8_t* applicationId, size_t applicationIdSize,
    uint8_t* publicKeyCert, size_t* publicKeyCertSize);

bool eicProvisioningStartPersonalization(
    EicProvisioning* ctx, int accessControlProfileCount, const int* entryCounts,
    size_t numEntryCounts, const char* docType, size_t docTypeLength,
    size_t expectedProofOfProvisioningingSize);

// The scratchSpace should be set to a buffer at least 512 bytes. It's done this
// way to avoid allocating stack space.
//
bool eicProvisioningAddAccessControlProfile(
    EicProvisioning* ctx, int id, const uint8_t* readerCertificate,
    size_t readerCertificateSize, bool userAuthenticationRequired,
    uint64_t timeoutMillis, uint64_t secureUserId, uint8_t outMac[28],
    uint8_t* scratchSpace, size_t scratchSpaceSize);

// The scratchSpace should be set to a buffer at least 512 bytes. It's done this
// way to avoid allocating stack space.
//
bool eicProvisioningBeginAddEntry(EicProvisioning* ctx,
                                  const uint8_t* accessControlProfileIds,
                                  size_t numAccessControlProfileIds,
                                  const char* nameSpace, size_t nameSpaceLength,
                                  const char* name, size_t nameLength,
                                  uint64_t entrySize, uint8_t* scratchSpace,
                                  size_t scratchSpaceSize);

// The outEncryptedContent array must be contentSize + 28 bytes long.
//
// The scratchSpace should be set to a buffer at least 512 bytes. It's done this
// way to avoid allocating stack space.
//
bool eicProvisioningAddEntryValue(
    EicProvisioning* ctx, const uint8_t* accessControlProfileIds,
    size_t numAccessControlProfileIds, const char* nameSpace,
    size_t nameSpaceLength, const char* name, size_t nameLength,
    const uint8_t* content, size_t contentSize, uint8_t* outEncryptedContent,
    uint8_t* scratchSpace, size_t scratchSpaceSize);

// The data returned in |signatureOfToBeSigned| contains the ECDSA signature of
// the ToBeSigned CBOR from RFC 8051 "4.4. Signing and Verification Process"
// where content is set to the ProofOfProvisioninging CBOR.
//
bool eicProvisioningFinishAddingEntries(
    EicProvisioning* ctx,
    uint8_t signatureOfToBeSigned[EIC_ECDSA_P256_SIGNATURE_SIZE]);

//
//
// The |encryptedCredentialKeys| array is set to AES-GCM-ENC(HBK, R,
// CredentialKeys, docType) where
//
//   CredentialKeys = [
//     bstr,   ; storageKey, a 128-bit AES key
//     bstr    ; credentialPrivKey, the private key for credentialKey
//     bstr    ; SHA-256(ProofOfProvisioning)
//   ]
//
// for feature version 202101. For feature version 202009 the third field was
// not present.
//
// Since |storageKey| is 16 bytes and |credentialPrivKey| is 32 bytes, the
// encoded CBOR for CredentialKeys is 86 bytes and consequently
// |encryptedCredentialKeys| will be no longer than 86 + 28 = 114 bytes.
//
bool eicProvisioningFinishGetCredentialData(
    EicProvisioning* ctx, const char* docType, size_t docTypeLength,
    uint8_t* encryptedCredentialKeys, size_t* encryptedCredentialKeysSize);

#ifdef __cplusplus
}
#endif

#endif  // ANDROID_HARDWARE_IDENTITY_EIC_PROVISIONING_H

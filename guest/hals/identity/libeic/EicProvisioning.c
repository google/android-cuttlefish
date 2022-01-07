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

#include "EicProvisioning.h"
#include "EicCommon.h"

bool eicProvisioningInit(EicProvisioning* ctx, bool testCredential) {
  eicMemSet(ctx, '\0', sizeof(EicProvisioning));
  ctx->testCredential = testCredential;
  if (!eicOpsRandom(ctx->storageKey, EIC_AES_128_KEY_SIZE)) {
    return false;
  }

  return true;
}

bool eicProvisioningInitForUpdate(EicProvisioning* ctx, bool testCredential,
                                  const char* docType, size_t docTypeLength,
                                  const uint8_t* encryptedCredentialKeys,
                                  size_t encryptedCredentialKeysSize) {
  uint8_t credentialKeys[EIC_CREDENTIAL_KEYS_CBOR_SIZE_FEATURE_VERSION_202101];

  // For feature version 202009 it's 52 bytes long and for feature version
  // 202101 it's 86 bytes (the additional data is the ProofOfProvisioning
  // SHA-256). We need to support loading all feature versions.
  //
  bool expectPopSha256 = false;
  if (encryptedCredentialKeysSize ==
      EIC_CREDENTIAL_KEYS_CBOR_SIZE_FEATURE_VERSION_202009 + 28) {
    /* do nothing */
  } else if (encryptedCredentialKeysSize ==
             EIC_CREDENTIAL_KEYS_CBOR_SIZE_FEATURE_VERSION_202101 + 28) {
    expectPopSha256 = true;
  } else {
    eicDebug("Unexpected size %zd for encryptedCredentialKeys",
             encryptedCredentialKeysSize);
    return false;
  }

  eicMemSet(ctx, '\0', sizeof(EicProvisioning));
  ctx->testCredential = testCredential;

  if (!eicOpsDecryptAes128Gcm(
          eicOpsGetHardwareBoundKey(testCredential), encryptedCredentialKeys,
          encryptedCredentialKeysSize,
          // DocType is the additionalAuthenticatedData
          (const uint8_t*)docType, docTypeLength, credentialKeys)) {
    eicDebug("Error decrypting CredentialKeys");
    return false;
  }

  // It's supposed to look like this;
  //
  // Feature version 202009:
  //
  //         CredentialKeys = [
  //              bstr,   ; storageKey, a 128-bit AES key
  //              bstr,   ; credentialPrivKey, the private key for credentialKey
  //         ]
  //
  // Feature version 202101:
  //
  //         CredentialKeys = [
  //              bstr,   ; storageKey, a 128-bit AES key
  //              bstr,   ; credentialPrivKey, the private key for credentialKey
  //              bstr    ; proofOfProvisioning SHA-256
  //         ]
  //
  // where storageKey is 16 bytes, credentialPrivateKey is 32 bytes, and
  // proofOfProvisioning SHA-256 is 32 bytes.
  //
  if (credentialKeys[0] !=
          (expectPopSha256 ? 0x83 : 0x82) ||  // array of two or three elements
      credentialKeys[1] != 0x50 ||            // 16-byte bstr
      credentialKeys[18] != 0x58 ||
      credentialKeys[19] != 0x20) {  // 32-byte bstr
    eicDebug("Invalid CBOR for CredentialKeys");
    return false;
  }
  if (expectPopSha256) {
    if (credentialKeys[52] != 0x58 ||
        credentialKeys[53] != 0x20) {  // 32-byte bstr
      eicDebug("Invalid CBOR for CredentialKeys");
      return false;
    }
  }
  eicMemCpy(ctx->storageKey, credentialKeys + 2, EIC_AES_128_KEY_SIZE);
  eicMemCpy(ctx->credentialPrivateKey, credentialKeys + 20,
            EIC_P256_PRIV_KEY_SIZE);
  // Note: We don't care about the previous ProofOfProvisioning SHA-256
  ctx->isUpdate = true;
  return true;
}

bool eicProvisioningCreateCredentialKey(
    EicProvisioning* ctx, const uint8_t* challenge, size_t challengeSize,
    const uint8_t* applicationId, size_t applicationIdSize,
    uint8_t* publicKeyCert, size_t* publicKeyCertSize) {
  if (ctx->isUpdate) {
    eicDebug("Cannot create CredentialKey on update");
    return false;
  }

  if (!eicOpsCreateCredentialKey(ctx->credentialPrivateKey, challenge,
                                 challengeSize, applicationId,
                                 applicationIdSize, ctx->testCredential,
                                 publicKeyCert, publicKeyCertSize)) {
    return false;
  }
  return true;
}

bool eicProvisioningStartPersonalization(
    EicProvisioning* ctx, int accessControlProfileCount, const int* entryCounts,
    size_t numEntryCounts, const char* docType, size_t docTypeLength,
    size_t expectedProofOfProvisioningSize) {
  if (numEntryCounts >= EIC_MAX_NUM_NAMESPACES) {
    return false;
  }
  if (accessControlProfileCount >= EIC_MAX_NUM_ACCESS_CONTROL_PROFILE_IDS) {
    return false;
  }

  ctx->numEntryCounts = numEntryCounts;
  if (numEntryCounts > EIC_MAX_NUM_NAMESPACES) {
    return false;
  }
  for (size_t n = 0; n < numEntryCounts; n++) {
    if (entryCounts[n] >= 256) {
      return false;
    }
    ctx->entryCounts[n] = entryCounts[n];
  }
  ctx->curNamespace = -1;
  ctx->curNamespaceNumProcessed = 0;

  eicCborInit(&ctx->cbor, NULL, 0);

  // What we're going to sign is the COSE ToBeSigned structure which
  // looks like the following:
  //
  // Sig_structure = [
  //   context : "Signature" / "Signature1" / "CounterSignature",
  //   body_protected : empty_or_serialized_map,
  //   ? sign_protected : empty_or_serialized_map,
  //   external_aad : bstr,
  //   payload : bstr
  //  ]
  //
  eicCborAppendArray(&ctx->cbor, 4);
  eicCborAppendStringZ(&ctx->cbor, "Signature1");

  // The COSE Encoded protected headers is just a single field with
  // COSE_LABEL_ALG (1) -> COSE_ALG_ECSDA_256 (-7). For simplicitly we just
  // hard-code the CBOR encoding:
  static const uint8_t coseEncodedProtectedHeaders[] = {0xa1, 0x01, 0x26};
  eicCborAppendByteString(&ctx->cbor, coseEncodedProtectedHeaders,
                          sizeof(coseEncodedProtectedHeaders));

  // We currently don't support Externally Supplied Data (RFC 8152 section 4.3)
  // so external_aad is the empty bstr
  static const uint8_t externalAad[0] = {};
  eicCborAppendByteString(&ctx->cbor, externalAad, sizeof(externalAad));

  // For the payload, the _encoded_ form follows here. We handle this by simply
  // opening a bstr, and then writing the CBOR. This requires us to know the
  // size of said bstr, ahead of time.
  eicCborBegin(&ctx->cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING,
               expectedProofOfProvisioningSize);
  ctx->expectedCborSizeAtEnd = expectedProofOfProvisioningSize + ctx->cbor.size;

  eicOpsSha256Init(&ctx->proofOfProvisioningDigester);
  eicCborEnableSecondaryDigesterSha256(&ctx->cbor,
                                       &ctx->proofOfProvisioningDigester);

  eicCborAppendArray(&ctx->cbor, 5);
  eicCborAppendStringZ(&ctx->cbor, "ProofOfProvisioning");
  eicCborAppendString(&ctx->cbor, docType, docTypeLength);

  eicCborAppendArray(&ctx->cbor, accessControlProfileCount);

  return true;
}

bool eicProvisioningAddAccessControlProfile(
    EicProvisioning* ctx, int id, const uint8_t* readerCertificate,
    size_t readerCertificateSize, bool userAuthenticationRequired,
    uint64_t timeoutMillis, uint64_t secureUserId, uint8_t outMac[28],
    uint8_t* scratchSpace, size_t scratchSpaceSize) {
  EicCbor cborBuilder;
  eicCborInit(&cborBuilder, scratchSpace, scratchSpaceSize);

  if (!eicCborCalcAccessControl(
          &cborBuilder, id, readerCertificate, readerCertificateSize,
          userAuthenticationRequired, timeoutMillis, secureUserId)) {
    return false;
  }

  // Calculate and return MAC
  uint8_t nonce[12];
  if (!eicOpsRandom(nonce, 12)) {
    return false;
  }
  if (!eicOpsEncryptAes128Gcm(ctx->storageKey, nonce, NULL, 0,
                              cborBuilder.buffer, cborBuilder.size, outMac)) {
    return false;
  }

  // The ACP CBOR in the provisioning receipt doesn't include secureUserId so
  // build it again.
  eicCborInit(&cborBuilder, scratchSpace, scratchSpaceSize);
  if (!eicCborCalcAccessControl(
          &cborBuilder, id, readerCertificate, readerCertificateSize,
          userAuthenticationRequired, timeoutMillis, 0 /* secureUserId */)) {
    return false;
  }

  // Append the CBOR from the local builder to the digester.
  eicCborAppend(&ctx->cbor, cborBuilder.buffer, cborBuilder.size);

  return true;
}

bool eicProvisioningBeginAddEntry(EicProvisioning* ctx,
                                  const uint8_t* accessControlProfileIds,
                                  size_t numAccessControlProfileIds,
                                  const char* nameSpace, size_t nameSpaceLength,
                                  const char* name, size_t nameLength,
                                  uint64_t entrySize, uint8_t* scratchSpace,
                                  size_t scratchSpaceSize) {
  uint8_t* additionalDataCbor = scratchSpace;
  const size_t additionalDataCborBufSize = scratchSpaceSize;
  size_t additionalDataCborSize;

  // We'll need to calc and store a digest of additionalData to check that it's
  // the same additionalData being passed in for every
  // eicProvisioningAddEntryValue() call...
  if (!eicCborCalcEntryAdditionalData(
          accessControlProfileIds, numAccessControlProfileIds, nameSpace,
          nameSpaceLength, name, nameLength, additionalDataCbor,
          additionalDataCborBufSize, &additionalDataCborSize,
          ctx->additionalDataSha256)) {
    return false;
  }

  if (ctx->curNamespace == -1) {
    ctx->curNamespace = 0;
    ctx->curNamespaceNumProcessed = 0;
    // Opens the main map: { * Namespace => [ + Entry ] }
    eicCborAppendMap(&ctx->cbor, ctx->numEntryCounts);
    eicCborAppendString(&ctx->cbor, nameSpace, nameSpaceLength);
    // Opens the per-namespace array: [ + Entry ]
    eicCborAppendArray(&ctx->cbor, ctx->entryCounts[ctx->curNamespace]);
  }

  if (ctx->curNamespaceNumProcessed == ctx->entryCounts[ctx->curNamespace]) {
    ctx->curNamespace += 1;
    ctx->curNamespaceNumProcessed = 0;
    eicCborAppendString(&ctx->cbor, nameSpace, nameSpaceLength);
    // Opens the per-namespace array: [ + Entry ]
    eicCborAppendArray(&ctx->cbor, ctx->entryCounts[ctx->curNamespace]);
  }

  eicCborAppendMap(&ctx->cbor, 3);
  eicCborAppendStringZ(&ctx->cbor, "name");
  eicCborAppendString(&ctx->cbor, name, nameLength);

  ctx->curEntrySize = entrySize;
  ctx->curEntryNumBytesReceived = 0;

  eicCborAppendStringZ(&ctx->cbor, "value");

  ctx->curNamespaceNumProcessed += 1;
  return true;
}

bool eicProvisioningAddEntryValue(
    EicProvisioning* ctx, const uint8_t* accessControlProfileIds,
    size_t numAccessControlProfileIds, const char* nameSpace,
    size_t nameSpaceLength, const char* name, size_t nameLength,
    const uint8_t* content, size_t contentSize, uint8_t* outEncryptedContent,
    uint8_t* scratchSpace, size_t scratchSpaceSize) {
  uint8_t* additionalDataCbor = scratchSpace;
  const size_t additionalDataCborBufSize = scratchSpaceSize;
  size_t additionalDataCborSize;
  uint8_t calculatedSha256[EIC_SHA256_DIGEST_SIZE];

  if (!eicCborCalcEntryAdditionalData(
          accessControlProfileIds, numAccessControlProfileIds, nameSpace,
          nameSpaceLength, name, nameLength, additionalDataCbor,
          additionalDataCborBufSize, &additionalDataCborSize,
          calculatedSha256)) {
    return false;
  }
  if (eicCryptoMemCmp(calculatedSha256, ctx->additionalDataSha256,
                      EIC_SHA256_DIGEST_SIZE) != 0) {
    eicDebug("SHA-256 mismatch of additionalData");
    return false;
  }

  eicCborAppend(&ctx->cbor, content, contentSize);

  uint8_t nonce[12];
  if (!eicOpsRandom(nonce, 12)) {
    return false;
  }
  if (!eicOpsEncryptAes128Gcm(ctx->storageKey, nonce, content, contentSize,
                              additionalDataCbor, additionalDataCborSize,
                              outEncryptedContent)) {
    return false;
  }

  // If done with this entry, close the map
  ctx->curEntryNumBytesReceived += contentSize;
  if (ctx->curEntryNumBytesReceived == ctx->curEntrySize) {
    eicCborAppendStringZ(&ctx->cbor, "accessControlProfiles");
    eicCborAppendArray(&ctx->cbor, numAccessControlProfileIds);
    for (size_t n = 0; n < numAccessControlProfileIds; n++) {
      eicCborAppendNumber(&ctx->cbor, accessControlProfileIds[n]);
    }
  }
  return true;
}

bool eicProvisioningFinishAddingEntries(
    EicProvisioning* ctx,
    uint8_t signatureOfToBeSigned[EIC_ECDSA_P256_SIGNATURE_SIZE]) {
  uint8_t cborSha256[EIC_SHA256_DIGEST_SIZE];

  eicCborAppendBool(&ctx->cbor, ctx->testCredential);
  eicCborFinal(&ctx->cbor, cborSha256);

  // This verifies that the correct expectedProofOfProvisioningSize value was
  // passed in at eicStartPersonalization() time.
  if (ctx->cbor.size != ctx->expectedCborSizeAtEnd) {
    eicDebug("CBOR size is %zd, was expecting %zd", ctx->cbor.size,
             ctx->expectedCborSizeAtEnd);
    return false;
  }

  if (!eicOpsEcDsa(ctx->credentialPrivateKey, cborSha256,
                   signatureOfToBeSigned)) {
    eicDebug("Error signing proofOfProvisioning");
    return false;
  }

  return true;
}

bool eicProvisioningFinishGetCredentialData(
    EicProvisioning* ctx, const char* docType, size_t docTypeLength,
    uint8_t* encryptedCredentialKeys, size_t* encryptedCredentialKeysSize) {
  EicCbor cbor;
  uint8_t cborBuf[86];

  if (*encryptedCredentialKeysSize < 86 + 28) {
    eicDebug("encryptedCredentialKeysSize is %zd which is insufficient");
    return false;
  }

  eicCborInit(&cbor, cborBuf, sizeof(cborBuf));
  eicCborAppendArray(&cbor, 3);
  eicCborAppendByteString(&cbor, ctx->storageKey, EIC_AES_128_KEY_SIZE);
  eicCborAppendByteString(&cbor, ctx->credentialPrivateKey,
                          EIC_P256_PRIV_KEY_SIZE);
  uint8_t popSha256[EIC_SHA256_DIGEST_SIZE];
  eicOpsSha256Final(&ctx->proofOfProvisioningDigester, popSha256);
  eicCborAppendByteString(&cbor, popSha256, EIC_SHA256_DIGEST_SIZE);
  if (cbor.size > sizeof(cborBuf)) {
    eicDebug("Exceeded buffer size");
    return false;
  }

  uint8_t nonce[12];
  if (!eicOpsRandom(nonce, 12)) {
    eicDebug("Error getting random");
    return false;
  }
  if (!eicOpsEncryptAes128Gcm(eicOpsGetHardwareBoundKey(ctx->testCredential),
                              nonce, cborBuf, cbor.size,
                              // DocType is the additionalAuthenticatedData
                              (const uint8_t*)docType, docTypeLength,
                              encryptedCredentialKeys)) {
    eicDebug("Error encrypting CredentialKeys");
    return false;
  }
  *encryptedCredentialKeysSize = cbor.size + 28;

  return true;
}

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

#include "EicPresentation.h"
#include "EicCommon.h"

#include <inttypes.h>

bool eicPresentationInit(EicPresentation* ctx, bool testCredential,
                         const char* docType, size_t docTypeLength,
                         const uint8_t* encryptedCredentialKeys,
                         size_t encryptedCredentialKeysSize) {
  uint8_t credentialKeys[EIC_CREDENTIAL_KEYS_CBOR_SIZE_FEATURE_VERSION_202101];
  bool expectPopSha256 = false;

  // For feature version 202009 it's 52 bytes long and for feature version
  // 202101 it's 86 bytes (the additional data is the ProofOfProvisioning
  // SHA-256). We need to support loading all feature versions.
  //
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

  eicMemSet(ctx, '\0', sizeof(EicPresentation));

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
  ctx->testCredential = testCredential;
  if (expectPopSha256) {
    eicMemCpy(ctx->proofOfProvisioningSha256, credentialKeys + 54,
              EIC_SHA256_DIGEST_SIZE);
  }
  return true;
}

bool eicPresentationGenerateSigningKeyPair(EicPresentation* ctx,
                                           const char* docType,
                                           size_t docTypeLength, time_t now,
                                           uint8_t* publicKeyCert,
                                           size_t* publicKeyCertSize,
                                           uint8_t signingKeyBlob[60]) {
  uint8_t signingKeyPriv[EIC_P256_PRIV_KEY_SIZE];
  uint8_t signingKeyPub[EIC_P256_PUB_KEY_SIZE];
  uint8_t cborBuf[64];

  // Generate the ProofOfBinding CBOR to include in the X.509 certificate in
  // IdentityCredentialAuthenticationKeyExtension CBOR. This CBOR is defined
  // by the following CDDL
  //
  //   ProofOfBinding = [
  //     "ProofOfBinding",
  //     bstr,                  // Contains the SHA-256 of ProofOfProvisioning
  //   ]
  //
  // This array may grow in the future if other information needs to be
  // conveyed.
  //
  // The bytes of ProofOfBinding is is represented as an OCTET_STRING
  // and stored at OID 1.3.6.1.4.1.11129.2.1.26.
  //

  EicCbor cbor;
  eicCborInit(&cbor, cborBuf, sizeof cborBuf);
  eicCborAppendArray(&cbor, 2);
  eicCborAppendStringZ(&cbor, "ProofOfBinding");
  eicCborAppendByteString(&cbor, ctx->proofOfProvisioningSha256,
                          EIC_SHA256_DIGEST_SIZE);
  if (cbor.size > sizeof(cborBuf)) {
    eicDebug("Exceeded buffer size");
    return false;
  }
  const uint8_t* proofOfBinding = cborBuf;
  size_t proofOfBindingSize = cbor.size;

  if (!eicOpsCreateEcKey(signingKeyPriv, signingKeyPub)) {
    eicDebug("Error creating signing key");
    return false;
  }

  const int secondsInOneYear = 365 * 24 * 60 * 60;
  time_t validityNotBefore = now;
  time_t validityNotAfter = now + secondsInOneYear;  // One year from now.
  if (!eicOpsSignEcKey(
          signingKeyPub, ctx->credentialPrivateKey, 1,
          "Android Identity Credential Key",                 // issuer CN
          "Android Identity Credential Authentication Key",  // subject CN
          validityNotBefore, validityNotAfter, proofOfBinding,
          proofOfBindingSize, publicKeyCert, publicKeyCertSize)) {
    eicDebug("Error creating certificate for signing key");
    return false;
  }

  uint8_t nonce[12];
  if (!eicOpsRandom(nonce, 12)) {
    eicDebug("Error getting random");
    return false;
  }
  if (!eicOpsEncryptAes128Gcm(
          ctx->storageKey, nonce, signingKeyPriv, sizeof(signingKeyPriv),
          // DocType is the additionalAuthenticatedData
          (const uint8_t*)docType, docTypeLength, signingKeyBlob)) {
    eicDebug("Error encrypting signing key");
    return false;
  }

  return true;
}

bool eicPresentationCreateEphemeralKeyPair(
    EicPresentation* ctx, uint8_t ephemeralPrivateKey[EIC_P256_PRIV_KEY_SIZE]) {
  uint8_t ephemeralPublicKey[EIC_P256_PUB_KEY_SIZE];
  if (!eicOpsCreateEcKey(ctx->ephemeralPrivateKey, ephemeralPublicKey)) {
    eicDebug("Error creating ephemeral key");
    return false;
  }
  eicMemCpy(ephemeralPrivateKey, ctx->ephemeralPrivateKey,
            EIC_P256_PRIV_KEY_SIZE);
  return true;
}

bool eicPresentationCreateAuthChallenge(EicPresentation* ctx,
                                        uint64_t* authChallenge) {
  do {
    if (!eicOpsRandom((uint8_t*)&(ctx->authChallenge), sizeof(uint64_t))) {
      eicDebug("Failed generating random challenge");
      return false;
    }
  } while (ctx->authChallenge == 0);
  eicDebug("Created auth challenge %" PRIu64, ctx->authChallenge);
  *authChallenge = ctx->authChallenge;
  return true;
}

// From "COSE Algorithms" registry
//
#define COSE_ALG_ECDSA_256 -7

bool eicPresentationValidateRequestMessage(
    EicPresentation* ctx, const uint8_t* sessionTranscript,
    size_t sessionTranscriptSize, const uint8_t* requestMessage,
    size_t requestMessageSize, int coseSignAlg,
    const uint8_t* readerSignatureOfToBeSigned,
    size_t readerSignatureOfToBeSignedSize) {
  if (ctx->readerPublicKeySize == 0) {
    eicDebug("No public key for reader");
    return false;
  }

  // Right now we only support ECDSA with SHA-256 (e.g. ES256).
  //
  if (coseSignAlg != COSE_ALG_ECDSA_256) {
    eicDebug(
        "COSE Signature algorithm for reader signature is %d, "
        "only ECDSA with SHA-256 is supported right now",
        coseSignAlg);
    return false;
  }

  // What we're going to verify is the COSE ToBeSigned structure which
  // looks like the following:
  //
  //   Sig_structure = [
  //     context : "Signature" / "Signature1" / "CounterSignature",
  //     body_protected : empty_or_serialized_map,
  //     ? sign_protected : empty_or_serialized_map,
  //     external_aad : bstr,
  //     payload : bstr
  //   ]
  //
  // So we're going to build that CBOR...
  //
  EicCbor cbor;
  eicCborInit(&cbor, NULL, 0);
  eicCborAppendArray(&cbor, 4);
  eicCborAppendStringZ(&cbor, "Signature1");

  // The COSE Encoded protected headers is just a single field with
  // COSE_LABEL_ALG (1) -> coseSignAlg (e.g. -7). For simplicitly we just
  // hard-code the CBOR encoding:
  static const uint8_t coseEncodedProtectedHeaders[] = {0xa1, 0x01, 0x26};
  eicCborAppendByteString(&cbor, coseEncodedProtectedHeaders,
                          sizeof(coseEncodedProtectedHeaders));

  // External_aad is the empty bstr
  static const uint8_t externalAad[0] = {};
  eicCborAppendByteString(&cbor, externalAad, sizeof(externalAad));

  // For the payload, the _encoded_ form follows here. We handle this by simply
  // opening a bstr, and then writing the CBOR. This requires us to know the
  // size of said bstr, ahead of time... the CBOR to be written is
  //
  //   ReaderAuthentication = [
  //      "ReaderAuthentication",
  //      SessionTranscript,
  //      ItemsRequestBytes
  //   ]
  //
  //   ItemsRequestBytes = #6.24(bstr .cbor ItemsRequest)
  //
  //   ReaderAuthenticationBytes = #6.24(bstr .cbor ReaderAuthentication)
  //
  // which is easily calculated below
  //
  size_t calculatedSize = 0;
  calculatedSize += 1;  // Array of size 3
  calculatedSize += 1;  // "ReaderAuthentication" less than 24 bytes
  calculatedSize +=
      sizeof("ReaderAuthentication") - 1;   // Don't include trailing NUL
  calculatedSize += sessionTranscriptSize;  // Already CBOR encoded
  calculatedSize += 2;  // Semantic tag EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR (24)
  calculatedSize += 1 + eicCborAdditionalLengthBytesFor(requestMessageSize);
  calculatedSize += requestMessageSize;

  // However note that we're authenticating ReaderAuthenticationBytes which
  // is a tagged bstr of the bytes of ReaderAuthentication. So need to get
  // that in front.
  size_t rabCalculatedSize = 0;
  rabCalculatedSize +=
      2;  // Semantic tag EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR (24)
  rabCalculatedSize += 1 + eicCborAdditionalLengthBytesFor(calculatedSize);
  rabCalculatedSize += calculatedSize;

  // Begin the bytestring for ReaderAuthenticationBytes;
  eicCborBegin(&cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING, rabCalculatedSize);

  eicCborAppendSemantic(&cbor, EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR);

  // Begins the bytestring for ReaderAuthentication;
  eicCborBegin(&cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING, calculatedSize);

  // And now that we know the size, let's fill it in...
  //
  size_t payloadOffset = cbor.size;
  eicCborBegin(&cbor, EIC_CBOR_MAJOR_TYPE_ARRAY, 3);
  eicCborAppendStringZ(&cbor, "ReaderAuthentication");
  eicCborAppend(&cbor, sessionTranscript, sessionTranscriptSize);
  eicCborAppendSemantic(&cbor, EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR);
  eicCborBegin(&cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING, requestMessageSize);
  eicCborAppend(&cbor, requestMessage, requestMessageSize);

  if (cbor.size != payloadOffset + calculatedSize) {
    eicDebug("CBOR size is %zd but we expected %zd", cbor.size,
             payloadOffset + calculatedSize);
    return false;
  }
  uint8_t toBeSignedDigest[EIC_SHA256_DIGEST_SIZE];
  eicCborFinal(&cbor, toBeSignedDigest);

  if (!eicOpsEcDsaVerifyWithPublicKey(
          toBeSignedDigest, EIC_SHA256_DIGEST_SIZE, readerSignatureOfToBeSigned,
          readerSignatureOfToBeSignedSize, ctx->readerPublicKey,
          ctx->readerPublicKeySize)) {
    eicDebug("Request message is not signed by public key");
    return false;
  }
  ctx->requestMessageValidated = true;
  return true;
}

// Validates the next certificate in the reader certificate chain.
bool eicPresentationPushReaderCert(EicPresentation* ctx,
                                   const uint8_t* certX509,
                                   size_t certX509Size) {
  // If we had a previous certificate, use its public key to validate this
  // certificate.
  if (ctx->readerPublicKeySize > 0) {
    if (!eicOpsX509CertSignedByPublicKey(certX509, certX509Size,
                                         ctx->readerPublicKey,
                                         ctx->readerPublicKeySize)) {
      eicDebug(
          "Certificate is not signed by public key in the previous "
          "certificate");
      return false;
    }
  }

  // Store the key of this certificate, this is used to validate the next
  // certificate and also ACPs with certificates that use the same public key...
  ctx->readerPublicKeySize = EIC_PRESENTATION_MAX_READER_PUBLIC_KEY_SIZE;
  if (!eicOpsX509GetPublicKey(certX509, certX509Size, ctx->readerPublicKey,
                              &ctx->readerPublicKeySize)) {
    eicDebug("Error extracting public key from certificate");
    return false;
  }
  if (ctx->readerPublicKeySize == 0) {
    eicDebug("Zero-length public key in certificate");
    return false;
  }

  return true;
}

bool eicPresentationSetAuthToken(
    EicPresentation* ctx, uint64_t challenge, uint64_t secureUserId,
    uint64_t authenticatorId, int hardwareAuthenticatorType, uint64_t timeStamp,
    const uint8_t* mac, size_t macSize, uint64_t verificationTokenChallenge,
    uint64_t verificationTokenTimestamp, int verificationTokenSecurityLevel,
    const uint8_t* verificationTokenMac, size_t verificationTokenMacSize) {
  // It doesn't make sense to accept any tokens if
  // eicPresentationCreateAuthChallenge() was never called.
  if (ctx->authChallenge == 0) {
    eicDebug(
        "Trying validate tokens when no auth-challenge was previously "
        "generated");
    return false;
  }
  // At least the verification-token must have the same challenge as what was
  // generated.
  if (verificationTokenChallenge != ctx->authChallenge) {
    eicDebug(
        "Challenge in verification token does not match the challenge "
        "previously generated");
    return false;
  }
  if (!eicOpsValidateAuthToken(
          challenge, secureUserId, authenticatorId, hardwareAuthenticatorType,
          timeStamp, mac, macSize, verificationTokenChallenge,
          verificationTokenTimestamp, verificationTokenSecurityLevel,
          verificationTokenMac, verificationTokenMacSize)) {
    return false;
  }
  ctx->authTokenChallenge = challenge;
  ctx->authTokenSecureUserId = secureUserId;
  ctx->authTokenTimestamp = timeStamp;
  ctx->verificationTokenTimestamp = verificationTokenTimestamp;
  return true;
}

static bool checkUserAuth(EicPresentation* ctx, bool userAuthenticationRequired,
                          int timeoutMillis, uint64_t secureUserId) {
  if (!userAuthenticationRequired) {
    return true;
  }

  if (secureUserId != ctx->authTokenSecureUserId) {
    eicDebug("secureUserId in profile differs from userId in authToken");
    return false;
  }

  // Only ACP with auth-on-every-presentation - those with timeout == 0 - need
  // the challenge to match...
  if (timeoutMillis == 0) {
    if (ctx->authTokenChallenge != ctx->authChallenge) {
      eicDebug("Challenge in authToken (%" PRIu64
               ") doesn't match the challenge "
               "that was created (%" PRIu64 ") for this session",
               ctx->authTokenChallenge, ctx->authChallenge);
      return false;
    }
  }

  uint64_t now = ctx->verificationTokenTimestamp;
  if (ctx->authTokenTimestamp > now) {
    eicDebug("Timestamp in authToken is in the future");
    return false;
  }

  if (timeoutMillis > 0) {
    if (now > ctx->authTokenTimestamp + timeoutMillis) {
      eicDebug("Deadline for authToken is in the past");
      return false;
    }
  }

  return true;
}

static bool checkReaderAuth(EicPresentation* ctx,
                            const uint8_t* readerCertificate,
                            size_t readerCertificateSize) {
  uint8_t publicKey[EIC_PRESENTATION_MAX_READER_PUBLIC_KEY_SIZE];
  size_t publicKeySize;

  if (readerCertificateSize == 0) {
    return true;
  }

  // Remember in this case certificate equality is done by comparing public
  // keys, not bitwise comparison of the certificates.
  //
  publicKeySize = EIC_PRESENTATION_MAX_READER_PUBLIC_KEY_SIZE;
  if (!eicOpsX509GetPublicKey(readerCertificate, readerCertificateSize,
                              publicKey, &publicKeySize)) {
    eicDebug("Error extracting public key from certificate");
    return false;
  }
  if (publicKeySize == 0) {
    eicDebug("Zero-length public key in certificate");
    return false;
  }

  if ((ctx->readerPublicKeySize != publicKeySize) ||
      (eicCryptoMemCmp(ctx->readerPublicKey, publicKey,
                       ctx->readerPublicKeySize) != 0)) {
    return false;
  }
  return true;
}

// Note: This function returns false _only_ if an error occurred check for
// access, _not_ whether access is granted. Whether access is granted is
// returned in |accessGranted|.
//
bool eicPresentationValidateAccessControlProfile(
    EicPresentation* ctx, int id, const uint8_t* readerCertificate,
    size_t readerCertificateSize, bool userAuthenticationRequired,
    int timeoutMillis, uint64_t secureUserId, const uint8_t mac[28],
    bool* accessGranted, uint8_t* scratchSpace, size_t scratchSpaceSize) {
  *accessGranted = false;
  if (id < 0 || id >= 32) {
    eicDebug("id value of %d is out of allowed range [0, 32[", id);
    return false;
  }

  // Validate the MAC
  EicCbor cborBuilder;
  eicCborInit(&cborBuilder, scratchSpace, scratchSpaceSize);
  if (!eicCborCalcAccessControl(
          &cborBuilder, id, readerCertificate, readerCertificateSize,
          userAuthenticationRequired, timeoutMillis, secureUserId)) {
    return false;
  }
  if (!eicOpsDecryptAes128Gcm(ctx->storageKey, mac, 28, cborBuilder.buffer,
                              cborBuilder.size, NULL)) {
    eicDebug("MAC for AccessControlProfile doesn't match");
    return false;
  }

  bool passedUserAuth = checkUserAuth(ctx, userAuthenticationRequired,
                                      timeoutMillis, secureUserId);
  bool passedReaderAuth =
      checkReaderAuth(ctx, readerCertificate, readerCertificateSize);

  ctx->accessControlProfileMaskValidated |= (1U << id);
  if (readerCertificateSize > 0) {
    ctx->accessControlProfileMaskUsesReaderAuth |= (1U << id);
  }
  if (!passedReaderAuth) {
    ctx->accessControlProfileMaskFailedReaderAuth |= (1U << id);
  }
  if (!passedUserAuth) {
    ctx->accessControlProfileMaskFailedUserAuth |= (1U << id);
  }

  if (passedUserAuth && passedReaderAuth) {
    *accessGranted = true;
    eicDebug("Access granted for id %d", id);
  }
  return true;
}

bool eicPresentationCalcMacKey(
    EicPresentation* ctx, const uint8_t* sessionTranscript,
    size_t sessionTranscriptSize,
    const uint8_t readerEphemeralPublicKey[EIC_P256_PUB_KEY_SIZE],
    const uint8_t signingKeyBlob[60], const char* docType, size_t docTypeLength,
    unsigned int numNamespacesWithValues, size_t expectedDeviceNamespacesSize) {
  uint8_t signingKeyPriv[EIC_P256_PRIV_KEY_SIZE];
  if (!eicOpsDecryptAes128Gcm(ctx->storageKey, signingKeyBlob, 60,
                              (const uint8_t*)docType, docTypeLength,
                              signingKeyPriv)) {
    eicDebug("Error decrypting signingKeyBlob");
    return false;
  }

  uint8_t sharedSecret[EIC_P256_COORDINATE_SIZE];
  if (!eicOpsEcdh(readerEphemeralPublicKey, signingKeyPriv, sharedSecret)) {
    eicDebug("ECDH failed");
    return false;
  }

  EicCbor cbor;
  eicCborInit(&cbor, NULL, 0);
  eicCborAppendSemantic(&cbor, EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR);
  eicCborAppendByteString(&cbor, sessionTranscript, sessionTranscriptSize);
  uint8_t salt[EIC_SHA256_DIGEST_SIZE];
  eicCborFinal(&cbor, salt);

  const uint8_t info[7] = {'E', 'M', 'a', 'c', 'K', 'e', 'y'};
  uint8_t derivedKey[32];
  if (!eicOpsHkdf(sharedSecret, EIC_P256_COORDINATE_SIZE, salt, sizeof(salt),
                  info, sizeof(info), derivedKey, sizeof(derivedKey))) {
    eicDebug("HKDF failed");
    return false;
  }

  eicCborInitHmacSha256(&ctx->cbor, NULL, 0, derivedKey, sizeof(derivedKey));
  ctx->buildCbor = true;

  // What we're going to calculate the HMAC-SHA256 is the COSE ToBeMaced
  // structure which looks like the following:
  //
  // MAC_structure = [
  //   context : "MAC" / "MAC0",
  //   protected : empty_or_serialized_map,
  //   external_aad : bstr,
  //   payload : bstr
  // ]
  //
  eicCborAppendArray(&ctx->cbor, 4);
  eicCborAppendStringZ(&ctx->cbor, "MAC0");

  // The COSE Encoded protected headers is just a single field with
  // COSE_LABEL_ALG (1) -> COSE_ALG_HMAC_256_256 (5). For simplicitly we just
  // hard-code the CBOR encoding:
  static const uint8_t coseEncodedProtectedHeaders[] = {0xa1, 0x01, 0x05};
  eicCborAppendByteString(&ctx->cbor, coseEncodedProtectedHeaders,
                          sizeof(coseEncodedProtectedHeaders));

  // We currently don't support Externally Supplied Data (RFC 8152 section 4.3)
  // so external_aad is the empty bstr
  static const uint8_t externalAad[0] = {};
  eicCborAppendByteString(&ctx->cbor, externalAad, sizeof(externalAad));

  // For the payload, the _encoded_ form follows here. We handle this by simply
  // opening a bstr, and then writing the CBOR. This requires us to know the
  // size of said bstr, ahead of time... the CBOR to be written is
  //
  //   DeviceAuthentication = [
  //      "DeviceAuthentication",
  //      SessionTranscript,
  //      DocType,                ; DocType as used in Documents structure in
  //      OfflineResponse DeviceNameSpacesBytes
  //   ]
  //
  //   DeviceNameSpacesBytes = #6.24(bstr .cbor DeviceNameSpaces)
  //
  //   DeviceAuthenticationBytes = #6.24(bstr .cbor DeviceAuthentication)
  //
  // which is easily calculated below
  //
  size_t calculatedSize = 0;
  calculatedSize += 1;  // Array of size 4
  calculatedSize += 1;  // "DeviceAuthentication" less than 24 bytes
  calculatedSize +=
      sizeof("DeviceAuthentication") - 1;   // Don't include trailing NUL
  calculatedSize += sessionTranscriptSize;  // Already CBOR encoded
  calculatedSize +=
      1 + eicCborAdditionalLengthBytesFor(docTypeLength) + docTypeLength;
  calculatedSize += 2;  // Semantic tag EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR (24)
  calculatedSize +=
      1 + eicCborAdditionalLengthBytesFor(expectedDeviceNamespacesSize);
  calculatedSize += expectedDeviceNamespacesSize;

  // However note that we're authenticating DeviceAuthenticationBytes which
  // is a tagged bstr of the bytes of DeviceAuthentication. So need to get
  // that in front.
  size_t dabCalculatedSize = 0;
  dabCalculatedSize +=
      2;  // Semantic tag EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR (24)
  dabCalculatedSize += 1 + eicCborAdditionalLengthBytesFor(calculatedSize);
  dabCalculatedSize += calculatedSize;

  // Begin the bytestring for DeviceAuthenticationBytes;
  eicCborBegin(&ctx->cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING, dabCalculatedSize);

  eicCborAppendSemantic(&ctx->cbor, EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR);

  // Begins the bytestring for DeviceAuthentication;
  eicCborBegin(&ctx->cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING, calculatedSize);

  eicCborAppendArray(&ctx->cbor, 4);
  eicCborAppendStringZ(&ctx->cbor, "DeviceAuthentication");
  eicCborAppend(&ctx->cbor, sessionTranscript, sessionTranscriptSize);
  eicCborAppendString(&ctx->cbor, docType, docTypeLength);

  // For the payload, the _encoded_ form follows here. We handle this by simply
  // opening a bstr, and then writing the CBOR. This requires us to know the
  // size of said bstr, ahead of time.
  eicCborAppendSemantic(&ctx->cbor, EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR);
  eicCborBegin(&ctx->cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING,
               expectedDeviceNamespacesSize);
  ctx->expectedCborSizeAtEnd = expectedDeviceNamespacesSize + ctx->cbor.size;

  eicCborAppendMap(&ctx->cbor, numNamespacesWithValues);
  return true;
}

bool eicPresentationStartRetrieveEntries(EicPresentation* ctx) {
  // HAL may use this object multiple times to retrieve data so need to reset
  // various state objects here.
  ctx->requestMessageValidated = false;
  ctx->buildCbor = false;
  ctx->accessControlProfileMaskValidated = 0;
  ctx->accessControlProfileMaskUsesReaderAuth = 0;
  ctx->accessControlProfileMaskFailedReaderAuth = 0;
  ctx->accessControlProfileMaskFailedUserAuth = 0;
  ctx->readerPublicKeySize = 0;
  return true;
}

EicAccessCheckResult eicPresentationStartRetrieveEntryValue(
    EicPresentation* ctx, const char* nameSpace, size_t nameSpaceLength,
    const char* name, size_t nameLength, unsigned int newNamespaceNumEntries,
    int32_t entrySize, const uint8_t* accessControlProfileIds,
    size_t numAccessControlProfileIds, uint8_t* scratchSpace,
    size_t scratchSpaceSize) {
  (void)entrySize;
  uint8_t* additionalDataCbor = scratchSpace;
  size_t additionalDataCborBufferSize = scratchSpaceSize;
  size_t additionalDataCborSize;

  if (newNamespaceNumEntries > 0) {
    eicCborAppendString(&ctx->cbor, nameSpace, nameSpaceLength);
    eicCborAppendMap(&ctx->cbor, newNamespaceNumEntries);
  }

  // We'll need to calc and store a digest of additionalData to check that it's
  // the same additionalData being passed in for every
  // eicPresentationRetrieveEntryValue() call...
  //
  ctx->accessCheckOk = false;
  if (!eicCborCalcEntryAdditionalData(
          accessControlProfileIds, numAccessControlProfileIds, nameSpace,
          nameSpaceLength, name, nameLength, additionalDataCbor,
          additionalDataCborBufferSize, &additionalDataCborSize,
          ctx->additionalDataSha256)) {
    return EIC_ACCESS_CHECK_RESULT_FAILED;
  }

  if (numAccessControlProfileIds == 0) {
    return EIC_ACCESS_CHECK_RESULT_NO_ACCESS_CONTROL_PROFILES;
  }

  // Access is granted if at least one of the profiles grants access.
  //
  // If an item is configured without any profiles, access is denied.
  //
  EicAccessCheckResult result = EIC_ACCESS_CHECK_RESULT_FAILED;
  for (size_t n = 0; n < numAccessControlProfileIds; n++) {
    int id = accessControlProfileIds[n];
    uint32_t idBitMask = (1 << id);

    // If the access control profile wasn't validated, this is an error and we
    // fail immediately.
    bool validated =
        ((ctx->accessControlProfileMaskValidated & idBitMask) != 0);
    if (!validated) {
      eicDebug("No ACP for profile id %d", id);
      return EIC_ACCESS_CHECK_RESULT_FAILED;
    }

    // Otherwise, we _did_ validate the profile. If none of the checks
    // failed, we're done
    bool failedUserAuth =
        ((ctx->accessControlProfileMaskFailedUserAuth & idBitMask) != 0);
    bool failedReaderAuth =
        ((ctx->accessControlProfileMaskFailedReaderAuth & idBitMask) != 0);
    if (!failedUserAuth && !failedReaderAuth) {
      result = EIC_ACCESS_CHECK_RESULT_OK;
      break;
    }
    // One of the checks failed, convey which one
    if (failedUserAuth) {
      result = EIC_ACCESS_CHECK_RESULT_USER_AUTHENTICATION_FAILED;
    } else {
      result = EIC_ACCESS_CHECK_RESULT_READER_AUTHENTICATION_FAILED;
    }
  }
  eicDebug("Result %d for name %s", result, name);

  if (result == EIC_ACCESS_CHECK_RESULT_OK) {
    eicCborAppendString(&ctx->cbor, name, nameLength);
    ctx->accessCheckOk = true;
  }
  return result;
}

// Note: |content| must be big enough to hold |encryptedContentSize| - 28 bytes.
bool eicPresentationRetrieveEntryValue(
    EicPresentation* ctx, const uint8_t* encryptedContent,
    size_t encryptedContentSize, uint8_t* content, const char* nameSpace,
    size_t nameSpaceLength, const char* name, size_t nameLength,
    const uint8_t* accessControlProfileIds, size_t numAccessControlProfileIds,
    uint8_t* scratchSpace, size_t scratchSpaceSize) {
  uint8_t* additionalDataCbor = scratchSpace;
  size_t additionalDataCborBufferSize = scratchSpaceSize;
  size_t additionalDataCborSize;

  uint8_t calculatedSha256[EIC_SHA256_DIGEST_SIZE];
  if (!eicCborCalcEntryAdditionalData(
          accessControlProfileIds, numAccessControlProfileIds, nameSpace,
          nameSpaceLength, name, nameLength, additionalDataCbor,
          additionalDataCborBufferSize, &additionalDataCborSize,
          calculatedSha256)) {
    return false;
  }

  if (eicCryptoMemCmp(calculatedSha256, ctx->additionalDataSha256,
                      EIC_SHA256_DIGEST_SIZE) != 0) {
    eicDebug("SHA-256 mismatch of additionalData");
    return false;
  }
  if (!ctx->accessCheckOk) {
    eicDebug("Attempting to retrieve a value for which access is not granted");
    return false;
  }

  if (!eicOpsDecryptAes128Gcm(ctx->storageKey, encryptedContent,
                              encryptedContentSize, additionalDataCbor,
                              additionalDataCborSize, content)) {
    eicDebug("Error decrypting content");
    return false;
  }

  eicCborAppend(&ctx->cbor, content, encryptedContentSize - 28);

  return true;
}

bool eicPresentationFinishRetrieval(EicPresentation* ctx,
                                    uint8_t* digestToBeMaced,
                                    size_t* digestToBeMacedSize) {
  if (!ctx->buildCbor) {
    *digestToBeMacedSize = 0;
    return true;
  }
  if (*digestToBeMacedSize != 32) {
    return false;
  }

  // This verifies that the correct expectedDeviceNamespacesSize value was
  // passed in at eicPresentationCalcMacKey() time.
  if (ctx->cbor.size != ctx->expectedCborSizeAtEnd) {
    eicDebug("CBOR size is %zd, was expecting %zd", ctx->cbor.size,
             ctx->expectedCborSizeAtEnd);
    return false;
  }
  eicCborFinal(&ctx->cbor, digestToBeMaced);
  return true;
}

bool eicPresentationDeleteCredential(
    EicPresentation* ctx, const char* docType, size_t docTypeLength,
    const uint8_t* challenge, size_t challengeSize, bool includeChallenge,
    size_t proofOfDeletionCborSize,
    uint8_t signatureOfToBeSigned[EIC_ECDSA_P256_SIGNATURE_SIZE]) {
  EicCbor cbor;

  eicCborInit(&cbor, NULL, 0);

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
  eicCborAppendArray(&cbor, 4);
  eicCborAppendStringZ(&cbor, "Signature1");

  // The COSE Encoded protected headers is just a single field with
  // COSE_LABEL_ALG (1) -> COSE_ALG_ECSDA_256 (-7). For simplicitly we just
  // hard-code the CBOR encoding:
  static const uint8_t coseEncodedProtectedHeaders[] = {0xa1, 0x01, 0x26};
  eicCborAppendByteString(&cbor, coseEncodedProtectedHeaders,
                          sizeof(coseEncodedProtectedHeaders));

  // We currently don't support Externally Supplied Data (RFC 8152 section 4.3)
  // so external_aad is the empty bstr
  static const uint8_t externalAad[0] = {};
  eicCborAppendByteString(&cbor, externalAad, sizeof(externalAad));

  // For the payload, the _encoded_ form follows here. We handle this by simply
  // opening a bstr, and then writing the CBOR. This requires us to know the
  // size of said bstr, ahead of time.
  eicCborBegin(&cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING, proofOfDeletionCborSize);

  // Finally, the CBOR that we're actually signing.
  eicCborAppendArray(&cbor, includeChallenge ? 4 : 3);
  eicCborAppendStringZ(&cbor, "ProofOfDeletion");
  eicCborAppendString(&cbor, docType, docTypeLength);
  if (includeChallenge) {
    eicCborAppendByteString(&cbor, challenge, challengeSize);
  }
  eicCborAppendBool(&cbor, ctx->testCredential);

  uint8_t cborSha256[EIC_SHA256_DIGEST_SIZE];
  eicCborFinal(&cbor, cborSha256);
  if (!eicOpsEcDsa(ctx->credentialPrivateKey, cborSha256,
                   signatureOfToBeSigned)) {
    eicDebug("Error signing proofOfDeletion");
    return false;
  }

  return true;
}

bool eicPresentationProveOwnership(
    EicPresentation* ctx, const char* docType, size_t docTypeLength,
    bool testCredential, const uint8_t* challenge, size_t challengeSize,
    size_t proofOfOwnershipCborSize,
    uint8_t signatureOfToBeSigned[EIC_ECDSA_P256_SIGNATURE_SIZE]) {
  EicCbor cbor;

  eicCborInit(&cbor, NULL, 0);

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
  eicCborAppendArray(&cbor, 4);
  eicCborAppendStringZ(&cbor, "Signature1");

  // The COSE Encoded protected headers is just a single field with
  // COSE_LABEL_ALG (1) -> COSE_ALG_ECSDA_256 (-7). For simplicitly we just
  // hard-code the CBOR encoding:
  static const uint8_t coseEncodedProtectedHeaders[] = {0xa1, 0x01, 0x26};
  eicCborAppendByteString(&cbor, coseEncodedProtectedHeaders,
                          sizeof(coseEncodedProtectedHeaders));

  // We currently don't support Externally Supplied Data (RFC 8152 section 4.3)
  // so external_aad is the empty bstr
  static const uint8_t externalAad[0] = {};
  eicCborAppendByteString(&cbor, externalAad, sizeof(externalAad));

  // For the payload, the _encoded_ form follows here. We handle this by simply
  // opening a bstr, and then writing the CBOR. This requires us to know the
  // size of said bstr, ahead of time.
  eicCborBegin(&cbor, EIC_CBOR_MAJOR_TYPE_BYTE_STRING,
               proofOfOwnershipCborSize);

  // Finally, the CBOR that we're actually signing.
  eicCborAppendArray(&cbor, 4);
  eicCborAppendStringZ(&cbor, "ProofOfOwnership");
  eicCborAppendString(&cbor, docType, docTypeLength);
  eicCborAppendByteString(&cbor, challenge, challengeSize);
  eicCborAppendBool(&cbor, testCredential);

  uint8_t cborSha256[EIC_SHA256_DIGEST_SIZE];
  eicCborFinal(&cbor, cborSha256);
  if (!eicOpsEcDsa(ctx->credentialPrivateKey, cborSha256,
                   signatureOfToBeSigned)) {
    eicDebug("Error signing proofOfDeletion");
    return false;
  }

  return true;
}

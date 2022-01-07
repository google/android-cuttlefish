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

#ifndef ANDROID_HARDWARE_IDENTITY_EIC_CBOR_H
#define ANDROID_HARDWARE_IDENTITY_EIC_CBOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "EicOps.h"

typedef enum {
  EIC_CBOR_DIGEST_TYPE_SHA256,
  EIC_CBOR_DIGEST_TYPE_HMAC_SHA256,
} EicCborDigestType;

/* EicCbor is a utility class to build CBOR data structures and calculate
 * digests on the fly.
 */
typedef struct {
  // Contains the size of the built CBOR, even if it exceeds bufferSize (will
  // never write to buffer beyond bufferSize though)
  size_t size;

  // The size of the buffer. Is zero if no data is recorded in which case
  // only digesting is performed.
  size_t bufferSize;

  // Whether we're producing a SHA-256 or HMAC-SHA256 digest.
  EicCborDigestType digestType;

  // The SHA-256 digester object.
  union {
    EicSha256Ctx sha256;
    EicHmacSha256Ctx hmacSha256;
  } digester;

  // The secondary digester, may be unset.
  EicSha256Ctx* secondaryDigesterSha256;

  // The buffer used for building up CBOR or NULL if bufferSize is 0.
  uint8_t* buffer;
} EicCbor;

/* Initializes an EicCbor.
 *
 * The given buffer will be used, up to bufferSize.
 *
 * If bufferSize is 0, buffer may be NULL.
 */
void eicCborInit(EicCbor* cbor, uint8_t* buffer, size_t bufferSize);

/* Like eicCborInit() but uses HMAC-SHA256 instead of SHA-256.
 */
void eicCborInitHmacSha256(EicCbor* cbor, uint8_t* buffer, size_t bufferSize,
                           const uint8_t* hmacKey, size_t hmacKeySize);

/* Enables a secondary digester.
 *
 * May be enabled midway through processing, this can be used to e.g. calculate
 * a digest of Sig_structure (for COSE_Sign1) and a separate digest of its
 * payload.
 */
void eicCborEnableSecondaryDigesterSha256(EicCbor* cbor, EicSha256Ctx* sha256);

/* Finishes building CBOR and returns the digest. */
void eicCborFinal(EicCbor* cbor, uint8_t digest[EIC_SHA256_DIGEST_SIZE]);

/* Appends CBOR data to the EicCbor. */
void eicCborAppend(EicCbor* cbor, const uint8_t* data, size_t size);

#define EIC_CBOR_MAJOR_TYPE_UNSIGNED 0
#define EIC_CBOR_MAJOR_TYPE_NEGATIVE 1
#define EIC_CBOR_MAJOR_TYPE_BYTE_STRING 2
#define EIC_CBOR_MAJOR_TYPE_STRING 3
#define EIC_CBOR_MAJOR_TYPE_ARRAY 4
#define EIC_CBOR_MAJOR_TYPE_MAP 5
#define EIC_CBOR_MAJOR_TYPE_SEMANTIC 6
#define EIC_CBOR_MAJOR_TYPE_SIMPLE 7

#define EIC_CBOR_SIMPLE_VALUE_FALSE 20
#define EIC_CBOR_SIMPLE_VALUE_TRUE 21

#define EIC_CBOR_SEMANTIC_TAG_ENCODED_CBOR 24

/* Begins a new CBOR value. */
void eicCborBegin(EicCbor* cbor, int majorType, uint64_t size);

/* Appends a bytestring. */
void eicCborAppendByteString(EicCbor* cbor, const uint8_t* data,
                             size_t dataSize);

/* Appends a UTF-8 string. */
void eicCborAppendString(EicCbor* cbor, const char* str, size_t strLength);

/* Appends a NUL-terminated UTF-8 string. */
void eicCborAppendStringZ(EicCbor* cbor, const char* str);

/* Appends a simple value. */
void eicCborAppendSimple(EicCbor* cbor, uint8_t simpleValue);

/* Appends a boolean. */
void eicCborAppendBool(EicCbor* cbor, bool value);

/* Appends a semantic */
void eicCborAppendSemantic(EicCbor* cbor, uint64_t value);

/* Appends an unsigned number. */
void eicCborAppendUnsigned(EicCbor* cbor, uint64_t value);

/* Appends a number. */
void eicCborAppendNumber(EicCbor* cbor, int64_t value);

/* Starts appending an array.
 *
 * After this numElements CBOR elements must follow.
 */
void eicCborAppendArray(EicCbor* cbor, size_t numElements);

/* Starts appending a map.
 *
 * After this numPairs pairs of CBOR elements must follow.
 */
void eicCborAppendMap(EicCbor* cbor, size_t numPairs);

/* Calculates how many bytes are needed to store a size. */
size_t eicCborAdditionalLengthBytesFor(size_t size);

bool eicCborCalcAccessControl(EicCbor* cborBuilder, int id,
                              const uint8_t* readerCertificate,
                              size_t readerCertificateSize,
                              bool userAuthenticationRequired,
                              uint64_t timeoutMillis, uint64_t secureUserId);

bool eicCborCalcEntryAdditionalData(
    const uint8_t* accessControlProfileIds, size_t numAccessControlProfileIds,
    const char* nameSpace, size_t nameSpaceLength, const char* name,
    size_t nameLength, uint8_t* cborBuffer, size_t cborBufferSize,
    size_t* outAdditionalDataCborSize,
    uint8_t additionalDataSha256[EIC_SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif  // ANDROID_HARDWARE_IDENTITY_EIC_CBOR_H

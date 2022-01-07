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

#ifndef ANDROID_HARDWARE_IDENTITY_EIC_OPS_H
#define ANDROID_HARDWARE_IDENTITY_EIC_OPS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

// Uncomment or define if debug messages are needed.
//
//#define EIC_DEBUG

#ifdef __cplusplus
extern "C" {
#endif

// The following defines must be set to something appropriate
//
//   EIC_SHA256_CONTEXT_SIZE - the size of EicSha256Ctx
//   EIC_HMAC_SHA256_CONTEXT_SIZE - the size of EicHmacSha256Ctx
//
// For example, if EicSha256Ctx is implemented using BoringSSL this would be
// defined as sizeof(SHA256_CTX).
//
// We expect the implementation to provide a header file with the name
// EicOpsImpl.h to do all this.
//
#include "EicOpsImpl.h"

#define EIC_SHA256_DIGEST_SIZE 32

// The size of a P-256 private key.
//
#define EIC_P256_PRIV_KEY_SIZE 32

// The size of a P-256 public key in uncompressed form.
//
// The public key is stored in uncompressed form, first the X coordinate, then
// the Y coordinate.
//
#define EIC_P256_PUB_KEY_SIZE 64

// Size of one of the coordinates in a curve-point.
//
#define EIC_P256_COORDINATE_SIZE 32

// The size of an ECSDA signature using P-256.
//
// The R and S values are stored here, first R then S.
//
#define EIC_ECDSA_P256_SIGNATURE_SIZE 64

#define EIC_AES_128_KEY_SIZE 16

// The following are definitions of implementation functions the
// underlying platform must provide.
//

struct EicSha256Ctx {
  uint8_t reserved[EIC_SHA256_CONTEXT_SIZE];
};
typedef struct EicSha256Ctx EicSha256Ctx;

struct EicHmacSha256Ctx {
  uint8_t reserved[EIC_HMAC_SHA256_CONTEXT_SIZE];
};
typedef struct EicHmacSha256Ctx EicHmacSha256Ctx;

#ifdef EIC_DEBUG
// Debug macro. Don't include a new-line in message.
//
#define eicDebug(...)                        \
  do {                                       \
    eicPrint("%s:%d: ", __FILE__, __LINE__); \
    eicPrint(__VA_ARGS__);                   \
    eicPrint("\n");                          \
  } while (0)
#else
#define eicDebug(...) \
  do {                \
  } while (0)
#endif

// Prints message which should include new-line character. Can be no-op.
//
// Don't use this from code, use eicDebug() instead.
//
#ifdef EIC_DEBUG
void eicPrint(const char* format, ...);
#else
inline void eicPrint(const char*, ...) {}
#endif

// Dumps data as pretty-printed hex. Can be no-op.
//
#ifdef EIC_DEBUG
void eicHexdump(const char* message, const uint8_t* data, size_t dataSize);
#else
inline void eicHexdump(const char*, const uint8_t*, size_t) {}
#endif

// Pretty-prints encoded CBOR. Can be no-op.
//
// If a byte-string is larger than |maxBStrSize| its contents will not be
// printed, instead the value of the form "<bstr size=1099016
// sha1=ef549cca331f73dfae2090e6a37c04c23f84b07b>" will be printed. Pass zero
// for |maxBStrSize| to disable this.
//
#ifdef EIC_DEBUG
void eicCborPrettyPrint(const uint8_t* cborData, size_t cborDataSize,
                        size_t maxBStrSize);
#else
inline void eicCborPrettyPrint(const uint8_t*, size_t, size_t) {}
#endif

// Memory setting, see memset(3).
void* eicMemSet(void* s, int c, size_t n);

// Memory copying, see memcpy(3).
void* eicMemCpy(void* dest, const void* src, size_t n);

// String length, see strlen(3).
size_t eicStrLen(const char* s);

// Memory compare, see CRYPTO_memcmp(3SSL)
//
// It takes an amount of time dependent on len, but independent of the contents
// of the memory regions pointed to by s1 and s2.
//
int eicCryptoMemCmp(const void* s1, const void* s2, size_t n);

// Random number generation.
bool eicOpsRandom(uint8_t* buf, size_t numBytes);

// If |testCredential| is true, returns the 128-bit AES Hardware-Bound Key (16
// bytes).
//
// Otherwise returns all zeroes (16 bytes).
//
const uint8_t* eicOpsGetHardwareBoundKey(bool testCredential);

// Encrypts |data| with |key| and |additionalAuthenticatedData| using |nonce|,
// returns the resulting (nonce || ciphertext || tag) in |encryptedData| which
// must be of size |dataSize| + 28.
bool eicOpsEncryptAes128Gcm(
    const uint8_t* key,    // Must be 16 bytes
    const uint8_t* nonce,  // Must be 12 bytes
    const uint8_t* data,   // May be NULL if size is 0
    size_t dataSize,
    const uint8_t* additionalAuthenticationData,  // May be NULL if size is 0
    size_t additionalAuthenticationDataSize, uint8_t* encryptedData);

// Decrypts |encryptedData| using |key| and |additionalAuthenticatedData|,
// returns resulting plaintext in |data| must be of size |encryptedDataSize|
// - 28.
//
// The format of |encryptedData| must be as specified in the
// encryptAes128Gcm() function.
bool eicOpsDecryptAes128Gcm(const uint8_t* key,  // Must be 16 bytes
                            const uint8_t* encryptedData,
                            size_t encryptedDataSize,
                            const uint8_t* additionalAuthenticationData,
                            size_t additionalAuthenticationDataSize,
                            uint8_t* data);

// Creates an EC key using the P-256 curve. The private key is written to
// |privateKey|. The public key is written to |publicKey|.
//
bool eicOpsCreateEcKey(uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                       uint8_t publicKey[EIC_P256_PUB_KEY_SIZE]);

// Generates CredentialKey plus an attestation certificate.
//
// The attestation certificate will be signed by the attestation keys the secure
// area has been provisioned with. The given |challenge| and |applicationId|
// will be used as will |testCredential|.
//
// The generated certificate will be in X.509 format and returned in |cert|
// and |certSize| must be set to the size of this array and this function will
// set it to the size of the certification chain on successfully return.
//
// This may return either a single certificate or an entire certificate
// chain. If it returns only a single certificate, the implementation of
// SecureHardwareProvisioningProxy::createCredentialKey() should amend the
// remainder of the certificate chain on the HAL side.
//
bool eicOpsCreateCredentialKey(uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                               const uint8_t* challenge, size_t challengeSize,
                               const uint8_t* applicationId,
                               size_t applicationIdSize, bool testCredential,
                               uint8_t* cert,
                               size_t* certSize);  // inout

// Generate an X.509 certificate for the key identified by |publicKey| which
// must be of the form returned by eicOpsCreateEcKey().
//
// If proofOfBinding is not NULL, it will be included as an OCTET_STRING
// X.509 extension at OID 1.3.6.1.4.1.11129.2.1.26.
//
// The certificate will be signed by the key identified by |signingKey| which
// must be of the form returned by eicOpsCreateEcKey().
//
bool eicOpsSignEcKey(const uint8_t publicKey[EIC_P256_PUB_KEY_SIZE],
                     const uint8_t signingKey[EIC_P256_PRIV_KEY_SIZE],
                     unsigned int serial, const char* issuerName,
                     const char* subjectName, time_t validityNotBefore,
                     time_t validityNotAfter, const uint8_t* proofOfBinding,
                     size_t proofOfBindingSize, uint8_t* cert,
                     size_t* certSize);  // inout

// Uses |privateKey| to create an ECDSA signature of some data (the SHA-256 must
// be given by |digestOfData|). Returns the signature in |signature|.
//
bool eicOpsEcDsa(const uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                 const uint8_t digestOfData[EIC_SHA256_DIGEST_SIZE],
                 uint8_t signature[EIC_ECDSA_P256_SIGNATURE_SIZE]);

// Performs Elliptic Curve Diffie-Helman.
//
bool eicOpsEcdh(const uint8_t publicKey[EIC_P256_PUB_KEY_SIZE],
                const uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                uint8_t sharedSecret[EIC_P256_COORDINATE_SIZE]);

// Performs HKDF.
//
bool eicOpsHkdf(const uint8_t* sharedSecret, size_t sharedSecretSize,
                const uint8_t* salt, size_t saltSize, const uint8_t* info,
                size_t infoSize, uint8_t* output, size_t outputSize);

// SHA-256 functions.
void eicOpsSha256Init(EicSha256Ctx* ctx);
void eicOpsSha256Update(EicSha256Ctx* ctx, const uint8_t* data, size_t len);
void eicOpsSha256Final(EicSha256Ctx* ctx,
                       uint8_t digest[EIC_SHA256_DIGEST_SIZE]);

// HMAC SHA-256 functions.
void eicOpsHmacSha256Init(EicHmacSha256Ctx* ctx, const uint8_t* key,
                          size_t keySize);
void eicOpsHmacSha256Update(EicHmacSha256Ctx* ctx, const uint8_t* data,
                            size_t len);
void eicOpsHmacSha256Final(EicHmacSha256Ctx* ctx,
                           uint8_t digest[EIC_SHA256_DIGEST_SIZE]);

// Extracts the public key in the given X.509 certificate.
//
// If the key is not an EC key, this function fails.
//
// Otherwise the public key is stored in uncompressed form in |publicKey| which
// size should be set in |publicKeySize|. On successful return |publicKeySize|
// is set to the length of the key. If there is not enough space, the function
// fails.
//
// (The public key returned is not necessarily a P-256 key, even if it is note
// that its size is not EIC_P256_PUBLIC_KEY_SIZE because of the leading 0x04.)
//
bool eicOpsX509GetPublicKey(const uint8_t* x509Cert, size_t x509CertSize,
                            uint8_t* publicKey, size_t* publicKeySize);

// Checks that the X.509 certificate given by |x509Cert| is signed by the public
// key given by |publicKey| which must be an EC key in uncompressed form (e.g.
// same formatt as returned by eicOpsX509GetPublicKey()).
//
bool eicOpsX509CertSignedByPublicKey(const uint8_t* x509Cert,
                                     size_t x509CertSize,
                                     const uint8_t* publicKey,
                                     size_t publicKeySize);

// Checks that |signature| is a signature of some data (given by |digest|),
// signed by the public key given by |publicKey|.
//
// The key must be an EC key in uncompressed form (e.g.  same format as returned
// by eicOpsX509GetPublicKey()).
//
// The format of the signature is the same encoding as the 'signature' field of
// COSE_Sign1 - that is, it's the R and S integers both with the same length as
// the key-size.
//
// The size of digest must match the size of the key.
//
bool eicOpsEcDsaVerifyWithPublicKey(const uint8_t* digest, size_t digestSize,
                                    const uint8_t* signature,
                                    size_t signatureSize,
                                    const uint8_t* publicKey,
                                    size_t publicKeySize);

// Validates that the passed in data constitutes a valid auth- and verification
// tokens.
//
bool eicOpsValidateAuthToken(
    uint64_t challenge, uint64_t secureUserId, uint64_t authenticatorId,
    int hardwareAuthenticatorType, uint64_t timeStamp, const uint8_t* mac,
    size_t macSize, uint64_t verificationTokenChallenge,
    uint64_t verificationTokenTimeStamp, int verificationTokenSecurityLevel,
    const uint8_t* verificationTokenMac, size_t verificationTokenMacSize);

#ifdef __cplusplus
}
#endif

#endif  // ANDROID_HARDWARE_IDENTITY_EIC_OPS_H

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

#define LOG_TAG "EicOpsImpl"

#include <optional>
#include <tuple>
#include <vector>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <string.h>

#include <android/hardware/identity/support/IdentityCredentialSupport.h>

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

#include "EicOps.h"

using ::std::map;
using ::std::optional;
using ::std::string;
using ::std::tuple;
using ::std::vector;

void* eicMemSet(void* s, int c, size_t n) { return memset(s, c, n); }

void* eicMemCpy(void* dest, const void* src, size_t n) {
  return memcpy(dest, src, n);
}

size_t eicStrLen(const char* s) { return strlen(s); }

int eicCryptoMemCmp(const void* s1, const void* s2, size_t n) {
  return CRYPTO_memcmp(s1, s2, n);
}

void eicOpsHmacSha256Init(EicHmacSha256Ctx* ctx, const uint8_t* key,
                          size_t keySize) {
  HMAC_CTX* realCtx = (HMAC_CTX*)ctx;
  HMAC_CTX_init(realCtx);
  if (HMAC_Init_ex(realCtx, key, keySize, EVP_sha256(), nullptr /* impl */) !=
      1) {
    LOG(ERROR) << "Error initializing HMAC_CTX";
  }
}

void eicOpsHmacSha256Update(EicHmacSha256Ctx* ctx, const uint8_t* data,
                            size_t len) {
  HMAC_CTX* realCtx = (HMAC_CTX*)ctx;
  if (HMAC_Update(realCtx, data, len) != 1) {
    LOG(ERROR) << "Error updating HMAC_CTX";
  }
}

void eicOpsHmacSha256Final(EicHmacSha256Ctx* ctx,
                           uint8_t digest[EIC_SHA256_DIGEST_SIZE]) {
  HMAC_CTX* realCtx = (HMAC_CTX*)ctx;
  unsigned int size = 0;
  if (HMAC_Final(realCtx, digest, &size) != 1) {
    LOG(ERROR) << "Error finalizing HMAC_CTX";
  }
  if (size != EIC_SHA256_DIGEST_SIZE) {
    LOG(ERROR) << "Expected 32 bytes from HMAC_Final, got " << size;
  }
}

void eicOpsSha256Init(EicSha256Ctx* ctx) {
  SHA256_CTX* realCtx = (SHA256_CTX*)ctx;
  SHA256_Init(realCtx);
}

void eicOpsSha256Update(EicSha256Ctx* ctx, const uint8_t* data, size_t len) {
  SHA256_CTX* realCtx = (SHA256_CTX*)ctx;
  SHA256_Update(realCtx, data, len);
}

void eicOpsSha256Final(EicSha256Ctx* ctx,
                       uint8_t digest[EIC_SHA256_DIGEST_SIZE]) {
  SHA256_CTX* realCtx = (SHA256_CTX*)ctx;
  SHA256_Final(digest, realCtx);
}

bool eicOpsRandom(uint8_t* buf, size_t numBytes) {
  optional<vector<uint8_t>> bytes =
      ::android::hardware::identity::support::getRandom(numBytes);
  if (!bytes.has_value()) {
    return false;
  }
  memcpy(buf, bytes.value().data(), numBytes);
  return true;
}

bool eicOpsEncryptAes128Gcm(
    const uint8_t* key,    // Must be 16 bytes
    const uint8_t* nonce,  // Must be 12 bytes
    const uint8_t* data,   // May be NULL if size is 0
    size_t dataSize,
    const uint8_t* additionalAuthenticationData,  // May be NULL if size is 0
    size_t additionalAuthenticationDataSize, uint8_t* encryptedData) {
  vector<uint8_t> cppKey;
  cppKey.resize(16);
  memcpy(cppKey.data(), key, 16);

  vector<uint8_t> cppData;
  cppData.resize(dataSize);
  if (dataSize > 0) {
    memcpy(cppData.data(), data, dataSize);
  }

  vector<uint8_t> cppAAD;
  cppAAD.resize(additionalAuthenticationDataSize);
  if (additionalAuthenticationDataSize > 0) {
    memcpy(cppAAD.data(), additionalAuthenticationData,
           additionalAuthenticationDataSize);
  }

  vector<uint8_t> cppNonce;
  cppNonce.resize(12);
  memcpy(cppNonce.data(), nonce, 12);

  optional<vector<uint8_t>> cppEncryptedData =
      android::hardware::identity::support::encryptAes128Gcm(cppKey, cppNonce,
                                                             cppData, cppAAD);
  if (!cppEncryptedData.has_value()) {
    return false;
  }

  memcpy(encryptedData, cppEncryptedData.value().data(),
         cppEncryptedData.value().size());
  return true;
}

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
                            uint8_t* data) {
  vector<uint8_t> keyVec;
  keyVec.resize(16);
  memcpy(keyVec.data(), key, 16);

  vector<uint8_t> encryptedDataVec;
  encryptedDataVec.resize(encryptedDataSize);
  if (encryptedDataSize > 0) {
    memcpy(encryptedDataVec.data(), encryptedData, encryptedDataSize);
  }

  vector<uint8_t> aadVec;
  aadVec.resize(additionalAuthenticationDataSize);
  if (additionalAuthenticationDataSize > 0) {
    memcpy(aadVec.data(), additionalAuthenticationData,
           additionalAuthenticationDataSize);
  }

  optional<vector<uint8_t>> decryptedDataVec =
      android::hardware::identity::support::decryptAes128Gcm(
          keyVec, encryptedDataVec, aadVec);
  if (!decryptedDataVec.has_value()) {
    eicDebug("Error decrypting data");
    return false;
  }
  if (decryptedDataVec.value().size() != encryptedDataSize - 28) {
    eicDebug("Decrypted data is size %zd, expected %zd",
             decryptedDataVec.value().size(), encryptedDataSize - 28);
    return false;
  }

  if (decryptedDataVec.value().size() > 0) {
    memcpy(data, decryptedDataVec.value().data(),
           decryptedDataVec.value().size());
  }
  return true;
}

bool eicOpsCreateEcKey(uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                       uint8_t publicKey[EIC_P256_PUB_KEY_SIZE]) {
  optional<vector<uint8_t>> keyPair =
      android::hardware::identity::support::createEcKeyPair();
  if (!keyPair) {
    eicDebug("Error creating EC keypair");
    return false;
  }
  optional<vector<uint8_t>> privKey =
      android::hardware::identity::support::ecKeyPairGetPrivateKey(
          keyPair.value());
  if (!privKey) {
    eicDebug("Error extracting private key");
    return false;
  }
  if (privKey.value().size() != EIC_P256_PRIV_KEY_SIZE) {
    eicDebug("Private key is %zd bytes, expected %zd", privKey.value().size(),
             (size_t)EIC_P256_PRIV_KEY_SIZE);
    return false;
  }

  optional<vector<uint8_t>> pubKey =
      android::hardware::identity::support::ecKeyPairGetPublicKey(
          keyPair.value());
  if (!pubKey) {
    eicDebug("Error extracting public key");
    return false;
  }
  // ecKeyPairGetPublicKey() returns 0x04 | x | y, we don't want the leading
  // 0x04.
  if (pubKey.value().size() != EIC_P256_PUB_KEY_SIZE + 1) {
    eicDebug("Public key is %zd bytes long, expected %zd",
             pubKey.value().size(), (size_t)EIC_P256_PRIV_KEY_SIZE + 1);
    return false;
  }

  memcpy(privateKey, privKey.value().data(), EIC_P256_PRIV_KEY_SIZE);
  memcpy(publicKey, pubKey.value().data() + 1, EIC_P256_PUB_KEY_SIZE);

  return true;
}

bool eicOpsCreateCredentialKey(uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                               const uint8_t* challenge, size_t challengeSize,
                               const uint8_t* applicationId,
                               size_t applicationIdSize, bool testCredential,
                               uint8_t* cert, size_t* certSize) {
  vector<uint8_t> challengeVec(challengeSize);
  memcpy(challengeVec.data(), challenge, challengeSize);

  vector<uint8_t> applicationIdVec(applicationIdSize);
  memcpy(applicationIdVec.data(), applicationId, applicationIdSize);

  optional<std::pair<vector<uint8_t>, vector<vector<uint8_t>>>> ret =
      android::hardware::identity::support::createEcKeyPairAndAttestation(
          challengeVec, applicationIdVec, testCredential);
  if (!ret) {
    eicDebug("Error generating CredentialKey and attestation");
    return false;
  }

  // Extract certificate chain.
  vector<uint8_t> flatChain =
      android::hardware::identity::support::certificateChainJoin(
          ret.value().second);
  if (*certSize < flatChain.size()) {
    eicDebug("Buffer for certificate is only %zd bytes long, need %zd bytes",
             *certSize, flatChain.size());
    return false;
  }
  memcpy(cert, flatChain.data(), flatChain.size());
  *certSize = flatChain.size();

  // Extract private key.
  optional<vector<uint8_t>> privKey =
      android::hardware::identity::support::ecKeyPairGetPrivateKey(
          ret.value().first);
  if (!privKey) {
    eicDebug("Error extracting private key");
    return false;
  }
  if (privKey.value().size() != EIC_P256_PRIV_KEY_SIZE) {
    eicDebug("Private key is %zd bytes, expected %zd", privKey.value().size(),
             (size_t)EIC_P256_PRIV_KEY_SIZE);
    return false;
  }

  memcpy(privateKey, privKey.value().data(), EIC_P256_PRIV_KEY_SIZE);

  return true;
}

bool eicOpsSignEcKey(const uint8_t publicKey[EIC_P256_PUB_KEY_SIZE],
                     const uint8_t signingKey[EIC_P256_PRIV_KEY_SIZE],
                     unsigned int serial, const char* issuerName,
                     const char* subjectName, time_t validityNotBefore,
                     time_t validityNotAfter, const uint8_t* proofOfBinding,
                     size_t proofOfBindingSize, uint8_t* cert,
                     size_t* certSize) {  // inout
  vector<uint8_t> signingKeyVec(EIC_P256_PRIV_KEY_SIZE);
  memcpy(signingKeyVec.data(), signingKey, EIC_P256_PRIV_KEY_SIZE);

  vector<uint8_t> pubKeyVec(EIC_P256_PUB_KEY_SIZE + 1);
  pubKeyVec[0] = 0x04;
  memcpy(pubKeyVec.data() + 1, publicKey, EIC_P256_PUB_KEY_SIZE);

  string serialDecimal = android::base::StringPrintf("%d", serial);

  map<string, vector<uint8_t>> extensions;
  if (proofOfBinding != nullptr) {
    vector<uint8_t> proofOfBindingVec(proofOfBinding,
                                      proofOfBinding + proofOfBindingSize);
    extensions["1.3.6.1.4.1.11129.2.1.26"] = proofOfBindingVec;
  }

  optional<vector<uint8_t>> certVec =
      android::hardware::identity::support::ecPublicKeyGenerateCertificate(
          pubKeyVec, signingKeyVec, serialDecimal, issuerName, subjectName,
          validityNotBefore, validityNotAfter, extensions);
  if (!certVec) {
    eicDebug("Error generating certificate");
    return false;
  }

  if (*certSize < certVec.value().size()) {
    eicDebug("Buffer for certificate is only %zd bytes long, need %zd bytes",
             *certSize, certVec.value().size());
    return false;
  }
  memcpy(cert, certVec.value().data(), certVec.value().size());
  *certSize = certVec.value().size();

  return true;
}

bool eicOpsEcDsa(const uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                 const uint8_t digestOfData[EIC_SHA256_DIGEST_SIZE],
                 uint8_t signature[EIC_ECDSA_P256_SIGNATURE_SIZE]) {
  vector<uint8_t> privKeyVec(EIC_P256_PRIV_KEY_SIZE);
  memcpy(privKeyVec.data(), privateKey, EIC_P256_PRIV_KEY_SIZE);

  vector<uint8_t> digestVec(EIC_SHA256_DIGEST_SIZE);
  memcpy(digestVec.data(), digestOfData, EIC_SHA256_DIGEST_SIZE);

  optional<vector<uint8_t>> derSignature =
      android::hardware::identity::support::signEcDsaDigest(privKeyVec,
                                                            digestVec);
  if (!derSignature) {
    eicDebug("Error signing data");
    return false;
  }

  ECDSA_SIG* sig;
  const unsigned char* p = derSignature.value().data();
  sig = d2i_ECDSA_SIG(nullptr, &p, derSignature.value().size());
  if (sig == nullptr) {
    eicDebug("Error decoding DER signature");
    return false;
  }

  if (BN_bn2binpad(sig->r, signature, 32) != 32) {
    eicDebug("Error encoding r");
    return false;
  }
  if (BN_bn2binpad(sig->s, signature + 32, 32) != 32) {
    eicDebug("Error encoding s");
    return false;
  }

  return true;
}

static const uint8_t hbkTest[16] = {0};
static const uint8_t hbkReal[16] = {0, 1, 2,  3,  4,  5,  6,  7,
                                    8, 9, 10, 11, 12, 13, 14, 15};

const uint8_t* eicOpsGetHardwareBoundKey(bool testCredential) {
  if (testCredential) {
    return hbkTest;
  }
  return hbkReal;
}

bool eicOpsValidateAuthToken(uint64_t /* challenge */,
                             uint64_t /* secureUserId */,
                             uint64_t /* authenticatorId */,
                             int /* hardwareAuthenticatorType */,
                             uint64_t /* timeStamp */, const uint8_t* /* mac */,
                             size_t /* macSize */,
                             uint64_t /* verificationTokenChallenge */,
                             uint64_t /* verificationTokenTimeStamp */,
                             int /* verificationTokenSecurityLevel */,
                             const uint8_t* /* verificationTokenMac */,
                             size_t /* verificationTokenMacSize */) {
  // Here's where we would validate the passed-in |authToken| to assure
  // ourselves that it comes from the e.g. biometric hardware and wasn't made up
  // by an attacker.
  //
  // However this involves calculating the MAC which requires access to the to
  // a pre-shared key which we don't have...
  //
  return true;
}

bool eicOpsX509GetPublicKey(const uint8_t* x509Cert, size_t x509CertSize,
                            uint8_t* publicKey, size_t* publicKeySize) {
  vector<uint8_t> chain;
  chain.resize(x509CertSize);
  memcpy(chain.data(), x509Cert, x509CertSize);
  optional<vector<uint8_t>> res =
      android::hardware::identity::support::certificateChainGetTopMostKey(
          chain);
  if (!res) {
    return false;
  }
  if (res.value().size() > *publicKeySize) {
    eicDebug("Public key size is %zd but buffer only has room for %zd bytes",
             res.value().size(), *publicKeySize);
    return false;
  }
  *publicKeySize = res.value().size();
  memcpy(publicKey, res.value().data(), *publicKeySize);
  eicDebug("Extracted %zd bytes public key from %zd bytes X.509 cert",
           *publicKeySize, x509CertSize);
  return true;
}

bool eicOpsX509CertSignedByPublicKey(const uint8_t* x509Cert,
                                     size_t x509CertSize,
                                     const uint8_t* publicKey,
                                     size_t publicKeySize) {
  vector<uint8_t> certVec(x509Cert, x509Cert + x509CertSize);
  vector<uint8_t> publicKeyVec(publicKey, publicKey + publicKeySize);
  return android::hardware::identity::support::certificateSignedByPublicKey(
      certVec, publicKeyVec);
}

bool eicOpsEcDsaVerifyWithPublicKey(const uint8_t* digest, size_t digestSize,
                                    const uint8_t* signature,
                                    size_t signatureSize,
                                    const uint8_t* publicKey,
                                    size_t publicKeySize) {
  vector<uint8_t> digestVec(digest, digest + digestSize);
  vector<uint8_t> signatureVec(signature, signature + signatureSize);
  vector<uint8_t> publicKeyVec(publicKey, publicKey + publicKeySize);

  vector<uint8_t> derSignature;
  if (!android::hardware::identity::support::ecdsaSignatureCoseToDer(
          signatureVec, derSignature)) {
    LOG(ERROR) << "Error convering signature to DER format";
    return false;
  }

  if (!android::hardware::identity::support::checkEcDsaSignature(
          digestVec, derSignature, publicKeyVec)) {
    LOG(ERROR) << "Signature check failed";
    return false;
  }
  return true;
}

bool eicOpsEcdh(const uint8_t publicKey[EIC_P256_PUB_KEY_SIZE],
                const uint8_t privateKey[EIC_P256_PRIV_KEY_SIZE],
                uint8_t sharedSecret[EIC_P256_COORDINATE_SIZE]) {
  vector<uint8_t> pubKeyVec(EIC_P256_PUB_KEY_SIZE + 1);
  pubKeyVec[0] = 0x04;
  memcpy(pubKeyVec.data() + 1, publicKey, EIC_P256_PUB_KEY_SIZE);

  vector<uint8_t> privKeyVec(EIC_P256_PRIV_KEY_SIZE);
  memcpy(privKeyVec.data(), privateKey, EIC_P256_PRIV_KEY_SIZE);

  optional<vector<uint8_t>> shared =
      android::hardware::identity::support::ecdh(pubKeyVec, privKeyVec);
  if (!shared) {
    LOG(ERROR) << "Error performing ECDH";
    return false;
  }
  if (shared.value().size() != EIC_P256_COORDINATE_SIZE) {
    LOG(ERROR) << "Unexpected size of shared secret " << shared.value().size()
               << " expected " << EIC_P256_COORDINATE_SIZE << " bytes";
    return false;
  }
  memcpy(sharedSecret, shared.value().data(), EIC_P256_COORDINATE_SIZE);
  return true;
}

bool eicOpsHkdf(const uint8_t* sharedSecret, size_t sharedSecretSize,
                const uint8_t* salt, size_t saltSize, const uint8_t* info,
                size_t infoSize, uint8_t* output, size_t outputSize) {
  vector<uint8_t> sharedSecretVec(sharedSecretSize);
  memcpy(sharedSecretVec.data(), sharedSecret, sharedSecretSize);
  vector<uint8_t> saltVec(saltSize);
  memcpy(saltVec.data(), salt, saltSize);
  vector<uint8_t> infoVec(infoSize);
  memcpy(infoVec.data(), info, infoSize);

  optional<vector<uint8_t>> result = android::hardware::identity::support::hkdf(
      sharedSecretVec, saltVec, infoVec, outputSize);
  if (!result) {
    LOG(ERROR) << "Error performing HKDF";
    return false;
  }
  if (result.value().size() != outputSize) {
    LOG(ERROR) << "Unexpected size of HKDF " << result.value().size()
               << " expected " << outputSize;
    return false;
  }
  memcpy(output, result.value().data(), outputSize);
  return true;
}

#ifdef EIC_DEBUG

void eicPrint(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void eicHexdump(const char* message, const uint8_t* data, size_t dataSize) {
  vector<uint8_t> dataVec(dataSize);
  memcpy(dataVec.data(), data, dataSize);
  android::hardware::identity::support::hexdump(message, dataVec);
}

void eicCborPrettyPrint(const uint8_t* cborData, size_t cborDataSize,
                        size_t maxBStrSize) {
  vector<uint8_t> cborDataVec(cborDataSize);
  memcpy(cborDataVec.data(), cborData, cborDataSize);
  string str = android::hardware::identity::support::cborPrettyPrint(
      cborDataVec, maxBStrSize, {});
  fprintf(stderr, "%s\n", str.c_str());
}

#endif  // EIC_DEBUG

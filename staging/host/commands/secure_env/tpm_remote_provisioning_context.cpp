/*
 * Copyright 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cassert>
#include <optional>

#include <android-base/logging.h>
#include <keymaster/cppcose/cppcose.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/hkdf.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_hmac.h"
#include "tpm_remote_provisioning_context.h"

using namespace cppcose;

TpmRemoteProvisioningContext::TpmRemoteProvisioningContext(
    TpmResourceManager& resource_manager)
    : resource_manager_(resource_manager) {
  std::tie(devicePrivKey_, bcc_) = GenerateBcc();
}

std::vector<uint8_t> TpmRemoteProvisioningContext::DeriveBytesFromHbk(
    const std::string& context, size_t num_bytes) const {
  // TODO(182928606) Derive using TPM-held secret.
  std::vector<uint8_t> fakeHbk(32);
  std::vector<uint8_t> result(num_bytes);
  if (!HKDF(result.data(), num_bytes,              //
            EVP_sha256(),                          //
            fakeHbk.data(), fakeHbk.size(),        //
            nullptr /* salt */, 0 /* salt len */,  //
            reinterpret_cast<const uint8_t*>(context.data()), context.size())) {
    // Should never fail. Even if it could the API has no way of reporting the
    // error.
    LOG(ERROR) << "Error calculating HMAC: " << ERR_peek_last_error();
  }

  return result;
}

std::unique_ptr<cppbor::Map> TpmRemoteProvisioningContext::CreateDeviceInfo()
    const {
  auto result = std::make_unique<cppbor::Map>();
  result->add(cppbor::Tstr("brand"), cppbor::Tstr("Google"));
  result->add(cppbor::Tstr("manufacturer"), cppbor::Tstr("Google"));
  result->add(cppbor::Tstr("product"),
              cppbor::Tstr("Cuttlefish Virtual Device"));
  result->canonicalize();
  return result;
}

std::pair<std::vector<uint8_t> /* privKey */, cppbor::Array /* BCC */>
TpmRemoteProvisioningContext::GenerateBcc() const {
  std::vector<uint8_t> privKey(ED25519_PRIVATE_KEY_LEN);
  std::vector<uint8_t> pubKey(ED25519_PUBLIC_KEY_LEN);

  // Length is hard-coded in the BoringCrypto API without a constant
  auto seed = DeriveBytesFromHbk("Device Key Seed", 32);
  ED25519_keypair_from_seed(pubKey.data(), privKey.data(), seed.data());

  auto coseKey = cppbor::Map()
                     .add(CoseKey::KEY_TYPE, OCTET_KEY_PAIR)
                     .add(CoseKey::ALGORITHM, EDDSA)
                     .add(CoseKey::CURVE, ED25519)
                     .add(CoseKey::KEY_OPS, VERIFY)
                     .add(CoseKey::PUBKEY_X, pubKey)
                     .canonicalize();
  auto sign1Payload =
      cppbor::Map()
          .add(1 /* Issuer */, "Issuer")
          .add(2 /* Subject */, "Subject")
          .add(-4670552 /* Subject Pub Key */, coseKey.encode())
          .add(-4670553 /* Key Usage (little-endian order) */,
               std::vector<uint8_t>{0x20} /* keyCertSign = 1<<5 */)
          .canonicalize()
          .encode();
  auto coseSign1 = constructCoseSign1(privKey,       /* signing key */
                                      cppbor::Map(), /* extra protected */
                                      sign1Payload, {} /* AAD */);
  assert(coseSign1);

  return {privKey,
          cppbor::Array().add(std::move(coseKey)).add(coseSign1.moveValue())};
}

std::optional<cppcose::HmacSha256>
TpmRemoteProvisioningContext::GenerateHmacSha256(
    const cppcose::bytevec& input) const {
  auto signing_key_builder = PrimaryKeyBuilder();
  signing_key_builder.SigningKey();
  signing_key_builder.UniqueData("Public Key Authentication Key");
  auto signing_key = signing_key_builder.CreateKey(resource_manager_);
  if (!signing_key) {
    LOG(ERROR) << "Could not make MAC key for authenticating the pubkey";
    return std::nullopt;
  }

  auto tpm_digest =
      TpmHmac(resource_manager_, signing_key->get(), TpmAuth(ESYS_TR_PASSWORD),
              input.data(), input.size());

  if (!tpm_digest) {
    LOG(ERROR) << "Could not calculate hmac";
    return std::nullopt;
  }

  cppcose::HmacSha256 hmac;
  if (tpm_digest->size != hmac.size()) {
    LOG(ERROR) << "TPM-generated digest was too short. Actual size: "
               << tpm_digest->size << " expected " << hmac.size() << " bytes";
    return std::nullopt;
  }

  std::copy(tpm_digest->buffer, tpm_digest->buffer + tpm_digest->size,
            hmac.begin());
  return hmac;
}

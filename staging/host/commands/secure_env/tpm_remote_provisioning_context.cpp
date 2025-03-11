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
#include <openssl/rand.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_hmac.h"
#include "tpm_remote_provisioning_context.h"
#include "tpm_resource_manager.h"

using namespace cppcose;

namespace cuttlefish {

TpmRemoteProvisioningContext::TpmRemoteProvisioningContext(
    TpmResourceManager& resource_manager)
    : resource_manager_(resource_manager) {
  std::tie(devicePrivKey_, bcc_) = GenerateBcc(/*testMode=*/false);
}

std::vector<uint8_t> TpmRemoteProvisioningContext::DeriveBytesFromHbk(
    const std::string& context, size_t num_bytes) const {
  PrimaryKeyBuilder key_builder;
  key_builder.SigningKey();
  key_builder.UniqueData("HardwareBoundKey");
  TpmObjectSlot key = key_builder.CreateKey(resource_manager_);

  auto hbk =
      TpmHmac(resource_manager_, key->get(), TpmAuth(ESYS_TR_PASSWORD),
              reinterpret_cast<const uint8_t*>(context.data()), context.size());

  std::vector<uint8_t> result(num_bytes);
  if (!HKDF(result.data(), num_bytes,              //
            EVP_sha256(),                          //
            hbk->buffer, hbk->size,                //
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
  result->add(cppbor::Tstr("model"), cppbor::Tstr("Virtual Device"));
  result->add(cppbor::Tstr("device"), cppbor::Tstr("Virtual Device"));
  if (bootloader_state_) {
    result->add(cppbor::Tstr("bootloader_state"),
                cppbor::Tstr(*bootloader_state_));
  }
  if (verified_boot_state_) {
    result->add(cppbor::Tstr("vb_state"), cppbor::Tstr(*verified_boot_state_));
  }
  if (vbmeta_digest_) {
    result->add(cppbor::Tstr("vbmeta_digest"), cppbor::Bstr(*vbmeta_digest_));
  }
  if (os_version_) {
    result->add(cppbor::Tstr("os_version"),
                cppbor::Tstr(std::to_string(*os_version_)));
  }
  if (os_patchlevel_) {
    result->add(cppbor::Tstr("system_patch_level"),
                cppbor::Uint(*os_patchlevel_));
  }
  if (boot_patchlevel_) {
    result->add(cppbor::Tstr("boot_patch_level"),
                cppbor::Uint(*boot_patchlevel_));
  }
  if (vendor_patchlevel_) {
    result->add(cppbor::Tstr("vendor_patch_level"),
                cppbor::Uint(*vendor_patchlevel_));
  }
  result->add(cppbor::Tstr("version"), cppbor::Uint(2));
  result->add(cppbor::Tstr("fused"), cppbor::Uint(0));
  result->add(cppbor::Tstr("security_level"), cppbor::Tstr("tee"));
  result->canonicalize();
  return result;
}

std::pair<std::vector<uint8_t> /* privKey */, cppbor::Array /* BCC */>
TpmRemoteProvisioningContext::GenerateBcc(bool testMode) const {
  std::vector<uint8_t> privKey(ED25519_PRIVATE_KEY_LEN);
  std::vector<uint8_t> pubKey(ED25519_PUBLIC_KEY_LEN);

  std::vector<uint8_t> seed;
  if (testMode) {
    // Length is hard-coded in the BoringCrypto API without a constant
    seed.resize(32);
    RAND_bytes(seed.data(), seed.size());
  } else {
    // TODO: Switch to P256 signing keys that are TPM-bound.
    seed = DeriveBytesFromHbk("BccKey", 32);
  }
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

void TpmRemoteProvisioningContext::SetSystemVersion(uint32_t os_version,
                                                    uint32_t os_patchlevel) {
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
}

void TpmRemoteProvisioningContext::SetVendorPatchlevel(
    uint32_t vendor_patchlevel) {
  vendor_patchlevel_ = vendor_patchlevel;
}

void TpmRemoteProvisioningContext::SetBootPatchlevel(uint32_t boot_patchlevel) {
  boot_patchlevel_ = boot_patchlevel;
}

void TpmRemoteProvisioningContext::SetVerifiedBootInfo(
    std::string_view boot_state, std::string_view bootloader_state,
    const std::vector<uint8_t>& vbmeta_digest) {
  verified_boot_state_ = boot_state;
  bootloader_state_ = bootloader_state;
  vbmeta_digest_ = vbmeta_digest;
}

ErrMsgOr<std::vector<uint8_t>>
TpmRemoteProvisioningContext::BuildProtectedDataPayload(
    bool isTestMode,                     //
    const std::vector<uint8_t>& macKey,  //
    const std::vector<uint8_t>& aad) const {
  std::vector<uint8_t> devicePrivKey;
  cppbor::Array bcc;
  if (isTestMode) {
    std::tie(devicePrivKey, bcc) = GenerateBcc(/*testMode=*/true);
  } else {
    devicePrivKey = devicePrivKey_;
    auto clone = bcc_.clone();
    if (!clone->asArray()) {
      return "The BCC is not an array";
    }
    bcc = std::move(*clone->asArray());
  }
  auto sign1 = constructCoseSign1(devicePrivKey, macKey, aad);
  if (!sign1) {
    return sign1.moveMessage();
  }
  return cppbor::Array().add(sign1.moveValue()).add(std::move(bcc)).encode();
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

}  // namespace cuttlefish

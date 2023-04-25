//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tpm_key_blob_maker.h"

#include <vector>

#include <android-base/logging.h>
#include <tss2/tss2_mu.h>
#include <tss2/tss2_rc.h>

#include "host/commands/secure_env/composite_serialization.h"
#include "host/commands/secure_env/encrypted_serializable.h"
#include "host/commands/secure_env/hmac_serializable.h"
#include "host/commands/secure_env/primary_key_builder.h"

namespace cuttlefish {

using keymaster::AuthorizationSet;
using keymaster::KeymasterKeyBlob;
using keymaster::Serializable;

static constexpr char kUniqueKey[] = "TpmKeyBlobMaker";

/**
 * Distinguish what properties the secure_env implementation handles. If
 * secure_env handles it, the property is put in `hw_enforced`. Otherwise, the
 * property is put in `sw_enforced`, and the Keystore process inside Android
 * will try to enforce the property.
 */
static keymaster_error_t SplitEnforcedProperties(
    const keymaster::AuthorizationSet& key_description,
    keymaster::AuthorizationSet* hw_enforced,
    keymaster::AuthorizationSet* sw_enforced,
    keymaster::AuthorizationSet* hidden) {
  for (auto& entry : key_description) {
    switch (entry.tag) {
      // These cannot be specified by the client.
      case KM_TAG_BOOT_PATCHLEVEL:
      case KM_TAG_ORIGIN:
      case KM_TAG_OS_PATCHLEVEL:
      case KM_TAG_OS_VERSION:
      case KM_TAG_ROOT_OF_TRUST:
      case KM_TAG_VENDOR_PATCHLEVEL:
        LOG(DEBUG) << "Root of trust and origin tags may not be specified";
        return KM_ERROR_INVALID_TAG;

      // These are hidden
      case KM_TAG_APPLICATION_DATA:
      case KM_TAG_APPLICATION_ID:
        hidden->push_back(entry);
        break;

      // These should not be in key descriptions because they're for operation
      // parameters.
      case KM_TAG_ASSOCIATED_DATA:
      case KM_TAG_AUTH_TOKEN:
      case KM_TAG_CONFIRMATION_TOKEN:
      case KM_TAG_INVALID:
      case KM_TAG_MAC_LENGTH:
      case KM_TAG_NONCE:
        LOG(DEBUG) << "Tag " << entry.tag
                   << " not allowed in key generation/import";
        break;

      // These are provided to support attestation key generation, but should
      // not be included in the key characteristics.
      case KM_TAG_ATTESTATION_APPLICATION_ID:
      case KM_TAG_ATTESTATION_CHALLENGE:
      case KM_TAG_ATTESTATION_ID_BRAND:
      case KM_TAG_ATTESTATION_ID_DEVICE:
      case KM_TAG_ATTESTATION_ID_IMEI:
      case KM_TAG_ATTESTATION_ID_SECOND_IMEI:
      case KM_TAG_ATTESTATION_ID_MANUFACTURER:
      case KM_TAG_ATTESTATION_ID_MEID:
      case KM_TAG_ATTESTATION_ID_MODEL:
      case KM_TAG_ATTESTATION_ID_PRODUCT:
      case KM_TAG_ATTESTATION_ID_SERIAL:
      case KM_TAG_CERTIFICATE_SERIAL:
      case KM_TAG_CERTIFICATE_SUBJECT:
      case KM_TAG_CERTIFICATE_NOT_BEFORE:
      case KM_TAG_CERTIFICATE_NOT_AFTER:
      case KM_TAG_RESET_SINCE_ID_ROTATION:
        break;

      // strongbox-only tags
      case KM_TAG_DEVICE_UNIQUE_ATTESTATION:
        LOG(DEBUG) << "Strongbox-only tag: " << entry.tag;
        return KM_ERROR_UNSUPPORTED_TAG;

      case KM_TAG_ROLLBACK_RESISTANT:
        return KM_ERROR_UNSUPPORTED_TAG;

      case KM_TAG_ROLLBACK_RESISTANCE:
        LOG(DEBUG) << "Rollback resistance is not implemented.";
        return KM_ERROR_ROLLBACK_RESISTANCE_UNAVAILABLE;

      // These are nominally HW tags, but we don't actually support HW key
      // attestation yet.
      case KM_TAG_ALLOW_WHILE_ON_BODY:
      case KM_TAG_EXPORTABLE:
      case KM_TAG_IDENTITY_CREDENTIAL_KEY:
      case KM_TAG_STORAGE_KEY:

      case KM_TAG_PURPOSE:
      case KM_TAG_ALGORITHM:
      case KM_TAG_KEY_SIZE:
      case KM_TAG_RSA_PUBLIC_EXPONENT:
      case KM_TAG_BLOB_USAGE_REQUIREMENTS:
      case KM_TAG_DIGEST:
      case KM_TAG_RSA_OAEP_MGF_DIGEST:
      case KM_TAG_PADDING:
      case KM_TAG_BLOCK_MODE:
      case KM_TAG_MIN_SECONDS_BETWEEN_OPS:
      case KM_TAG_MAX_USES_PER_BOOT:
      case KM_TAG_USER_SECURE_ID:
      case KM_TAG_NO_AUTH_REQUIRED:
      case KM_TAG_AUTH_TIMEOUT:
      case KM_TAG_CALLER_NONCE:
      case KM_TAG_MIN_MAC_LENGTH:
      case KM_TAG_KDF:
      case KM_TAG_EC_CURVE:
      case KM_TAG_ECIES_SINGLE_HASH_MODE:
      case KM_TAG_USER_AUTH_TYPE:
      case KM_TAG_EARLY_BOOT_ONLY:
      case KM_TAG_UNLOCKED_DEVICE_REQUIRED:
        hw_enforced->push_back(entry);
        break;

      // The remaining tags are all software.
      case KM_TAG_ACTIVE_DATETIME:
      case KM_TAG_ALL_APPLICATIONS:
      case KM_TAG_ALL_USERS:
      case KM_TAG_BOOTLOADER_ONLY:
      case KM_TAG_CREATION_DATETIME:
      case KM_TAG_INCLUDE_UNIQUE_ID:
      case KM_TAG_MAX_BOOT_LEVEL:
      case KM_TAG_ORIGINATION_EXPIRE_DATETIME:
      case KM_TAG_TRUSTED_CONFIRMATION_REQUIRED:
      case KM_TAG_TRUSTED_USER_PRESENCE_REQUIRED:
      case KM_TAG_UNIQUE_ID:
      case KM_TAG_USAGE_COUNT_LIMIT:
      case KM_TAG_USAGE_EXPIRE_DATETIME:
      case KM_TAG_USER_ID:
        sw_enforced->push_back(entry);
        break;
    }
  }

  return KM_ERROR_OK;
}

static KeymasterKeyBlob SerializableToKeyBlob(
    const Serializable& serializable) {
  std::vector<uint8_t> data(serializable.SerializedSize() + 1);
  uint8_t* buf = data.data();
  uint8_t* buf_end = buf + data.size();
  buf = serializable.Serialize(buf, buf_end);
  if (buf != (buf_end - 1)) {
    LOG(ERROR) << "Serialized size did not match up with actual usage.";
    return {};
  }
  return KeymasterKeyBlob(data.data(), buf - data.data());
}


TpmKeyBlobMaker::TpmKeyBlobMaker(TpmResourceManager& resource_manager)
    : resource_manager_(resource_manager) {
}

keymaster_error_t TpmKeyBlobMaker::CreateKeyBlob(
    const AuthorizationSet& key_description,
    keymaster_key_origin_t origin,
    const KeymasterKeyBlob& key_material,
    KeymasterKeyBlob* blob,
    AuthorizationSet* hw_enforced,
    AuthorizationSet* sw_enforced) const {
  AuthorizationSet hidden;
  auto rc = SplitEnforcedProperties(key_description, hw_enforced, sw_enforced,
                                    &hidden);
  if (rc != KM_ERROR_OK) {
    return rc;
  }
  hw_enforced->push_back(keymaster::TAG_ORIGIN, origin);

  // TODO(schuffelen): Set the os level and patch level properly.
  hw_enforced->push_back(keymaster::TAG_OS_VERSION, os_version_);
  hw_enforced->push_back(keymaster::TAG_OS_PATCHLEVEL, os_patchlevel_);

  if (vendor_patchlevel_) {
    hw_enforced->push_back(keymaster::TAG_VENDOR_PATCHLEVEL,
                           *vendor_patchlevel_);
  }
  if (boot_patchlevel_) {
    hw_enforced->push_back(keymaster::TAG_BOOT_PATCHLEVEL, *boot_patchlevel_);
  }

  return UnvalidatedCreateKeyBlob(key_material, *hw_enforced, *sw_enforced,
                                  hidden, blob);
}

keymaster_error_t TpmKeyBlobMaker::UnvalidatedCreateKeyBlob(
    const KeymasterKeyBlob& key_material, const AuthorizationSet& hw_enforced,
    const AuthorizationSet& sw_enforced, const AuthorizationSet& hidden,
    KeymasterKeyBlob* blob) const {
  keymaster::Buffer key_material_buffer(
      key_material.key_material, key_material.key_material_size);
  AuthorizationSet hw_enforced_mutable = hw_enforced;
  AuthorizationSet sw_enforced_mutable = sw_enforced;
  CompositeSerializable sensitive_material(
      {&key_material_buffer, &hw_enforced_mutable, &sw_enforced_mutable});
  auto parent_key_fn = ParentKeyCreator(kUniqueKey);
  EncryptedSerializable encryption(
      resource_manager_, parent_key_fn, sensitive_material);
  auto signing_key_fn = SigningKeyCreator(kUniqueKey);
  // TODO(b/154956668) The "hidden" tags should also be mixed into the TPM ACL
  // so that the TPM requires them to be presented to unwrap the key. This is
  // necessary to meet the requirement that full breach of KeyMint means an
  // attacker cannot unwrap keys w/o the application id/data.
  HmacSerializable sign_check(resource_manager_, signing_key_fn,
                              TPM2_SHA256_DIGEST_SIZE, &encryption, &hidden);
  auto generated_blob = SerializableToKeyBlob(sign_check);
  LOG(VERBOSE) << "Keymaster key size: " << generated_blob.key_material_size;
  if (generated_blob.key_material_size != 0) {
    *blob = generated_blob;
    return KM_ERROR_OK;
  }
  LOG(ERROR) << "Failed to serialize key.";
  return KM_ERROR_UNKNOWN_ERROR;
}

keymaster_error_t TpmKeyBlobMaker::UnwrapKeyBlob(
    const keymaster_key_blob_t& blob, AuthorizationSet* hw_enforced,
    AuthorizationSet* sw_enforced, const AuthorizationSet& hidden,
    KeymasterKeyBlob* key_material) const {
  keymaster::Buffer key_material_buffer(blob.key_material_size);
  CompositeSerializable sensitive_material(
      {&key_material_buffer, hw_enforced, sw_enforced});
  auto parent_key_fn = ParentKeyCreator(kUniqueKey);
  EncryptedSerializable encryption(
      resource_manager_, parent_key_fn, sensitive_material);
  auto signing_key_fn = SigningKeyCreator(kUniqueKey);
  HmacSerializable sign_check(resource_manager_, signing_key_fn,
                              TPM2_SHA256_DIGEST_SIZE, &encryption, &hidden);
  auto buf = blob.key_material;
  auto buf_end = buf + blob.key_material_size;
  if (!sign_check.Deserialize(&buf, buf_end)) {
    LOG(ERROR) << "Failed to deserialize key.";
    return KM_ERROR_INVALID_KEY_BLOB;
  }
  if (key_material_buffer.available_read() == 0) {
    LOG(ERROR) << "Key material was corrupted and the size was too large";
    return KM_ERROR_INVALID_KEY_BLOB;
  }
  *key_material = KeymasterKeyBlob(
      key_material_buffer.peek_read(), key_material_buffer.available_read());
  return KM_ERROR_OK;
}

keymaster_error_t TpmKeyBlobMaker::SetSystemVersion(
    uint32_t os_version, uint32_t os_patchlevel) {
  // TODO(b/201561154): Only accept new values of these from the bootloader
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
  return KM_ERROR_OK;
}

keymaster_error_t TpmKeyBlobMaker::SetVendorPatchlevel(uint32_t patchlevel) {
  // TODO(b/201561154): Only accept new values of these from the bootloader
  vendor_patchlevel_ = patchlevel;
  return KM_ERROR_OK;
}

keymaster_error_t TpmKeyBlobMaker::SetBootPatchlevel(uint32_t boot_patchlevel) {
  // TODO(b/201561154): Only accept new values of these from the bootloader
  boot_patchlevel_ = boot_patchlevel;
  return KM_ERROR_OK;
}

}  // namespace cuttlefish

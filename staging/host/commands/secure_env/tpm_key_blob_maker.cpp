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
    keymaster::AuthorizationSet* sw_enforced) {
  for (auto& entry : key_description) {
    switch (entry.tag) {
      case KM_TAG_PURPOSE:
      case KM_TAG_ALGORITHM:
      case KM_TAG_KEY_SIZE:
      case KM_TAG_RSA_PUBLIC_EXPONENT:
      case KM_TAG_BLOB_USAGE_REQUIREMENTS:
      case KM_TAG_DIGEST:
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
      case KM_TAG_ORIGIN:
      case KM_TAG_OS_VERSION:
      case KM_TAG_OS_PATCHLEVEL:
      case KM_TAG_EARLY_BOOT_ONLY:
      case KM_TAG_UNLOCKED_DEVICE_REQUIRED:
        hw_enforced->push_back(entry);
        break;
      default:
        sw_enforced->push_back(entry);
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
  std::set<keymaster_tag_t> protected_tags = {
    KM_TAG_ROOT_OF_TRUST,
    KM_TAG_ORIGIN,
    KM_TAG_OS_VERSION,
    KM_TAG_OS_PATCHLEVEL,
  };
  for (auto tag : protected_tags) {
    if (key_description.Contains(tag)) {
      LOG(ERROR) << "Invalid tag " << tag;
      return KM_ERROR_INVALID_TAG;
    }
  }
  auto rc =
      SplitEnforcedProperties(key_description, hw_enforced, sw_enforced);
  if (rc != KM_ERROR_OK) {
    return rc;
  }
  hw_enforced->push_back(keymaster::TAG_ORIGIN, origin);

  // TODO(schuffelen): Set the os level and patch level properly.
  hw_enforced->push_back(keymaster::TAG_OS_VERSION, os_version_);
  hw_enforced->push_back(keymaster::TAG_OS_PATCHLEVEL, os_patchlevel_);

  return UnvalidatedCreateKeyBlob(key_material, *hw_enforced, *sw_enforced,
                                  blob);
}

keymaster_error_t TpmKeyBlobMaker::UnvalidatedCreateKeyBlob(
    const KeymasterKeyBlob& key_material, const AuthorizationSet& hw_enforced,
    const AuthorizationSet& sw_enforced, KeymasterKeyBlob* blob) const {
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
  HmacSerializable sign_check(
      resource_manager_, signing_key_fn, TPM2_SHA256_DIGEST_SIZE, &encryption);
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
    const keymaster_key_blob_t& blob,
    AuthorizationSet* hw_enforced,
    AuthorizationSet* sw_enforced,
    KeymasterKeyBlob* key_material) const {
  keymaster::Buffer key_material_buffer(blob.key_material_size);
  CompositeSerializable sensitive_material(
      {&key_material_buffer, hw_enforced, sw_enforced});
  auto parent_key_fn = ParentKeyCreator(kUniqueKey);
  EncryptedSerializable encryption(
      resource_manager_, parent_key_fn, sensitive_material);
  auto signing_key_fn = SigningKeyCreator(kUniqueKey);
  HmacSerializable sign_check(
      resource_manager_, signing_key_fn, TPM2_SHA256_DIGEST_SIZE, &encryption);
  auto buf = blob.key_material;
  auto buf_end = buf + blob.key_material_size;
  if (!sign_check.Deserialize(&buf, buf_end)) {
    LOG(ERROR) << "Failed to deserialize key.";
    return KM_ERROR_UNKNOWN_ERROR;
  }
  if (key_material_buffer.available_read() == 0) {
    LOG(ERROR) << "Key material was corrupted and the size was too large";
    return KM_ERROR_UNKNOWN_ERROR;
  }
  *key_material = KeymasterKeyBlob(
      key_material_buffer.peek_read(), key_material_buffer.available_read());
  return KM_ERROR_OK;
}

keymaster_error_t TpmKeyBlobMaker::SetSystemVersion(
    uint32_t os_version, uint32_t os_patchlevel) {
  // TODO(b/155697375): Only accept new values of these from the bootloader
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
  return KM_ERROR_OK;
}

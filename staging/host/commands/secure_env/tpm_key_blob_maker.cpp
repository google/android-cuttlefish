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

using keymaster::AuthorizationSet;
using keymaster::KeymasterKeyBlob;
using keymaster::Serializable;

/**
 * Returns a TPM reference to a key used for integrity checking on wrapped keys.
 */
static TpmObjectSlot SigningKey(TpmResourceManager* resource_manager) {
  TPM2B_AUTH authValue = {};
  auto rc =
      Esys_TR_SetAuth(resource_manager->Esys(), ESYS_TR_RH_OWNER, &authValue);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_TR_SetAuth failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }

  TPMT_PUBLIC public_area;
  public_area.nameAlg = TPM2_ALG_SHA1;
  public_area.type = TPM2_ALG_KEYEDHASH;
  public_area.objectAttributes |= TPMA_OBJECT_SIGN_ENCRYPT;
  public_area.objectAttributes |= TPMA_OBJECT_USERWITHAUTH;
  public_area.objectAttributes |= TPMA_OBJECT_SENSITIVEDATAORIGIN;
  public_area.parameters.keyedHashDetail.scheme.scheme = TPM2_ALG_HMAC;
  public_area.parameters.keyedHashDetail.scheme.details.hmac.hashAlg = TPM2_ALG_SHA1;

  TPM2B_TEMPLATE public_template = {};
  size_t offset = 0;
  rc = Tss2_MU_TPMT_PUBLIC_Marshal(&public_area, &public_template.buffer[0],
                                   sizeof(public_template.buffer), &offset);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Tss2_MU_TPMT_PUBLIC_Marshal failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  public_template.size = offset;

  TPM2B_SENSITIVE_CREATE in_sensitive = {};

  auto key_slot = resource_manager->ReserveSlot();
  if (!key_slot) {
    LOG(ERROR) << "No slots available";
    return {};
  }
  ESYS_TR raw_handle;
  // TODO(b/154956668): Define better ACLs on these keys.
  // Since this is a primary key, it's generated deterministically. It would
  // also be possible to generate this once and hold it in storage.
  rc = Esys_CreateLoaded(
    /* esysContext */ resource_manager->Esys(),
    /* primaryHandle */ ESYS_TR_RH_OWNER,
    /* shandle1 */ ESYS_TR_PASSWORD,
    /* shandle2 */ ESYS_TR_NONE,
    /* shandle3 */ ESYS_TR_NONE,
    /* inSensitive */ &in_sensitive,
    /* inPublic */ &public_template,
    /* objectHandle */ &raw_handle,
    /* outPrivate */ nullptr,
    /* outPublic */ nullptr);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_CreateLoaded failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  key_slot->set(raw_handle);
  return key_slot;
}

static TpmObjectSlot ParentKey(TpmResourceManager* resource_manager) {
  TPM2B_AUTH authValue = {};
  auto rc =
      Esys_TR_SetAuth(resource_manager->Esys(), ESYS_TR_RH_PLATFORM, &authValue);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_TR_SetAuth failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }

  TPMT_PUBLIC public_area = {
    .type = TPM2_ALG_SYMCIPHER,
    .nameAlg = TPM2_ALG_SHA256,
    .objectAttributes = (TPMA_OBJECT_USERWITHAUTH |
                         TPMA_OBJECT_RESTRICTED |
                         TPMA_OBJECT_DECRYPT |
                         TPMA_OBJECT_FIXEDTPM |
                         TPMA_OBJECT_FIXEDPARENT |
                         TPMA_OBJECT_SENSITIVEDATAORIGIN),
    .authPolicy.size = 0,
    .parameters.symDetail.sym = {
      .algorithm = TPM2_ALG_AES,
      .keyBits.aes = 128, // The default maximum AES key size in the simulator.
      .mode.aes = TPM2_ALG_CFB,
    },
  };

  TPM2B_TEMPLATE public_template = {};
  size_t offset = 0;
  rc = Tss2_MU_TPMT_PUBLIC_Marshal(&public_area, &public_template.buffer[0],
                                   sizeof(public_template.buffer), &offset);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Tss2_MU_TPMT_PUBLIC_Marshal failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  public_template.size = offset;

  TPM2B_SENSITIVE_CREATE in_sensitive = {};

  auto key_slot = resource_manager->ReserveSlot();
  if (!key_slot) {
    LOG(ERROR) << "No key slots available";
    return {};
  }
  ESYS_TR raw_handle;
  // TODO(b/154956668): Define better ACLs on these keys.
  TPM2B_PUBLIC* key_public = nullptr;
  TPM2B_PRIVATE* key_private = nullptr;
  rc = Esys_CreateLoaded(
    /* esysContext */ resource_manager->Esys(),
    /* primaryHandle */ ESYS_TR_RH_PLATFORM,
    /* shandle1 */ ESYS_TR_PASSWORD,
    /* shandle2 */ ESYS_TR_NONE,
    /* shandle3 */ ESYS_TR_NONE,
    /* inSensitive */ &in_sensitive,
    /* inPublic */ &public_template,
    /* objectHandle */ &raw_handle,
    /* outPrivate */ &key_private,
    /* outPublic */ &key_public);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_CreateLoaded failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  Esys_Free(key_private);
  Esys_Free(key_public);
  key_slot->set(raw_handle);
  return key_slot;
}

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
  // TODO(schuffelen): Put the things we enforce in hw_enforced.
  (void) hw_enforced;
  *sw_enforced = key_description;
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


TpmKeyBlobMaker::TpmKeyBlobMaker(TpmResourceManager* resource_manager)
    : resource_manager_(resource_manager) {
}

keymaster_error_t TpmKeyBlobMaker::CreateKeyBlob(
    const AuthorizationSet& key_description,
    keymaster_key_origin_t origin,
    const KeymasterKeyBlob& key_material,
    KeymasterKeyBlob* blob,
    AuthorizationSet* hw_enforced,
    AuthorizationSet* sw_enforced) const {
  (void) origin; // TODO(schuffelen): Figure out how this is used
  auto rc =
      SplitEnforcedProperties(key_description, hw_enforced, sw_enforced);
  if (rc != KM_ERROR_OK) {
    return rc;
  }
  keymaster::Buffer key_material_buffer(
      key_material.key_material, key_material.key_material_size);
  CompositeSerializable sensitive_material(
      {&key_material_buffer, hw_enforced, sw_enforced});
  EncryptedSerializable encryption(
      resource_manager_, ParentKey, &sensitive_material);
  HmacSerializable sign_check(
      resource_manager_, SigningKey, TPM2_SHA1_DIGEST_SIZE, &encryption);
  auto generated_blob = SerializableToKeyBlob(sign_check);
  LOG(DEBUG) << "Keymaster key size: " << generated_blob.key_material_size;
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
  EncryptedSerializable encryption(
      resource_manager_, ParentKey, &sensitive_material);
  HmacSerializable sign_check(
      resource_manager_, SigningKey, TPM2_SHA1_DIGEST_SIZE, &encryption);
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

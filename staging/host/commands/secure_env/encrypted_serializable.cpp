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

#include "encrypted_serializable.h"

#include <vector>

#include <android-base/logging.h>

#include "host/commands/secure_env/tpm_auth.h"
#include "host/commands/secure_env/tpm_encrypt_decrypt.h"
#include "host/commands/secure_env/tpm_serialize.h"

EncryptedSerializable::EncryptedSerializable(
    TpmResourceManager* resource_manager,
    std::function<TpmObjectSlot(TpmResourceManager*)> parent_key_fn,
    Serializable* wrapped) :
    resource_manager_(resource_manager),
    parent_key_fn_(parent_key_fn),
    wrapped_(wrapped) {
}

static bool CreateKey(
    TpmResourceManager* resource_manager, // in
    ESYS_TR parent_key, // in
    TPM2B_PUBLIC* key_public_out, // out
    TPM2B_PRIVATE* key_private_out, // out
    TpmObjectSlot* key_slot_out) { // out
  TPM2B_AUTH authValue = {};
  auto rc = Esys_TR_SetAuth(resource_manager->Esys(), parent_key, &authValue);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_TR_SetAuth failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return false;
  }

  TPMT_PUBLIC public_area = {
    .type = TPM2_ALG_SYMCIPHER,
    .nameAlg = TPM2_ALG_SHA256,
    .objectAttributes = (TPMA_OBJECT_USERWITHAUTH |
                         TPMA_OBJECT_DECRYPT |
                         TPMA_OBJECT_SIGN_ENCRYPT |
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
    return false;
  }
  public_template.size = offset;

  TPM2B_SENSITIVE_CREATE in_sensitive = {};

  auto key_slot = resource_manager->ReserveSlot();
  if (!key_slot) {
    LOG(ERROR) << "No slots available";
    return false;
  }
  ESYS_TR raw_handle;
  // TODO(b/154956668): Define better ACLs on these keys.
  TPM2B_PUBLIC* key_public = nullptr;
  TPM2B_PRIVATE* key_private = nullptr;
  // TODO(schuffelen): Use Esys_Create when key_slot is NULL
  rc = Esys_CreateLoaded(
    /* esysContext */ resource_manager->Esys(),
    /* primaryHandle */ parent_key,
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
    return false;
  }
  CHECK(key_public != nullptr) << "key_public was not assigned.";
  CHECK(key_private != nullptr) << "key_private was not assigned.";
  *key_public_out = *key_public;
  *key_private_out = *key_private;
  key_slot->set(raw_handle);
  Esys_Free(key_public);
  Esys_Free(key_private);
  if (key_slot_out) {
    rc = Esys_TR_SetAuth(resource_manager->Esys(), raw_handle, &authValue);
    if (rc != TSS2_RC_SUCCESS) {
      LOG(ERROR) << "Esys_TR_SetAuth failed with return code " << rc
                << " (" << Tss2_RC_Decode(rc) << ")";
      return false;
    }
  }
  if (key_slot_out) {
    *key_slot_out = key_slot;
  }
  return true;
}

static TpmObjectSlot LoadKey(
    TpmResourceManager* resource_manager,
    ESYS_TR parent_key,
    const TPM2B_PUBLIC* key_public,
    const TPM2B_PRIVATE* key_private) {
  // TODO
  ESYS_TR raw_handle;
  auto key_slot = resource_manager->ReserveSlot();
  if (!key_slot) {
    LOG(ERROR) << "No slots available";
    return {};
  }
  auto rc = Esys_Load(
      resource_manager->Esys(),
      parent_key,
      ESYS_TR_PASSWORD,
      ESYS_TR_NONE,
      ESYS_TR_NONE,
      key_private,
      key_public,
      &raw_handle);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_Load failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  key_slot->set(raw_handle);
  return key_slot;
}

static constexpr uint32_t BLOCK_SIZE = 16;

static uint32_t RoundUpToBlockSize(uint32_t num) {
  return num % BLOCK_SIZE == 0 ? num : num + (BLOCK_SIZE - (num % BLOCK_SIZE));
}

size_t EncryptedSerializable::SerializedSize() const {
  TPM2B_PUBLIC key_public;
  TPM2B_PRIVATE key_private;
  auto parent = parent_key_fn_(resource_manager_);
  if (!CreateKey(
      resource_manager_, parent->get(), &key_public, &key_private, nullptr)) {
    LOG(ERROR) << "Unable to create key";
    return 0;
  }
  // Assumes all created keys will have the same size.
  SerializeTpmKeyPublic serialize_public(&key_public);
  SerializeTpmKeyPrivate serialize_private(&key_private);
  auto encrypted_size = RoundUpToBlockSize(wrapped_->SerializedSize());
  return serialize_public.SerializedSize()
    + serialize_private.SerializedSize()
    + sizeof(uint32_t)
    + sizeof(uint32_t)
    + encrypted_size;
}

uint8_t* EncryptedSerializable::Serialize(
    uint8_t* buf, const uint8_t* end) const {
  TPM2B_PUBLIC key_public;
  TPM2B_PRIVATE key_private;
  auto parent = parent_key_fn_(resource_manager_);
  if (!parent) {
    LOG(ERROR) << "Unable to load encryption parent key";
    return buf;
  }
  TpmObjectSlot key_slot;
  if (!CreateKey(
      resource_manager_, parent->get(), &key_public, &key_private, &key_slot)) {
    LOG(ERROR) << "Unable to create key";
    return buf;
  }

  auto wrapped_size = wrapped_->SerializedSize();
  auto encrypted_size = RoundUpToBlockSize(wrapped_size);
  std::vector<uint8_t> unencrypted(encrypted_size + 1, 0);
  auto unencrypted_buf = unencrypted.data();
  auto unencrypted_buf_end = unencrypted_buf + unencrypted.size();
  auto next_buf = wrapped_->Serialize(unencrypted_buf, unencrypted_buf_end);
  if (next_buf - unencrypted_buf != wrapped_size) {
    LOG(ERROR) << "Size mismatch on wrapped data";
    return buf;
  }
  std::vector<uint8_t> encrypted(encrypted_size, 0);
  if (!TpmEncrypt(
      resource_manager_->Esys(),
      key_slot->get(),
      TpmAuth(ESYS_TR_PASSWORD),
      unencrypted.data(),
      encrypted.data(),
      encrypted_size)) {
    LOG(ERROR) << "Encryption failed";
    return buf;
  }
  SerializeTpmKeyPublic serialize_public(&key_public);
  SerializeTpmKeyPrivate serialize_private(&key_private);

  buf = serialize_public.Serialize(buf, end);
  buf = serialize_private.Serialize(buf, end);
  buf = keymaster::append_uint32_to_buf(buf, end, BLOCK_SIZE);
  buf = keymaster::append_uint32_to_buf(buf, end, wrapped_size);
  buf = keymaster::append_to_buf(buf, end, encrypted.data(), encrypted_size);
  return buf;
}

bool EncryptedSerializable::Deserialize(
    const uint8_t** buf_ptr, const uint8_t* end) {
  auto parent_key = parent_key_fn_(resource_manager_);
  if (!parent_key) {
    LOG(ERROR) << "Unable to load encryption parent key";
    return false;
  }
  TPM2B_PUBLIC key_public;
  SerializeTpmKeyPublic serialize_public(&key_public);
  if (!serialize_public.Deserialize(buf_ptr, end)) {
    LOG(ERROR) << "Unable to deserialize key public part";
    return false;
  }
  TPM2B_PRIVATE key_private;
  SerializeTpmKeyPrivate serialize_private(&key_private);
  if (!serialize_private.Deserialize(buf_ptr, end)) {
    LOG(ERROR) << "Unable to deserialize key private part";
    return false;
  }
  auto key_slot =
      LoadKey(resource_manager_, parent_key->get(), &key_public, &key_private);
  if (!key_slot) {
    LOG(ERROR) << "Failed to load key into TPM";
    return false;
  }
  uint32_t block_size = 0;
  if (!keymaster::copy_uint32_from_buf(buf_ptr, end, &block_size)) {
    LOG(ERROR) << "Failed to read block size";
    return false;
  }
  if (block_size != BLOCK_SIZE) {
    LOG(ERROR) << "Unexpected block size: was " << block_size
               << ", expected " << BLOCK_SIZE;
    return false;
  }
  uint32_t wrapped_size = 0;
  if (!keymaster::copy_uint32_from_buf(buf_ptr, end, &wrapped_size)) {
    LOG(ERROR) << "Failed to read wrapped size";
    return false;
  }
  uint32_t encrypted_size = RoundUpToBlockSize(wrapped_size);
  std::vector<uint8_t> encrypted_data(encrypted_size, 0);
  if (!keymaster::copy_from_buf(
      buf_ptr, end, encrypted_data.data(), encrypted_size)) {
    LOG(ERROR) << "Failed to read encrypted data";
    return false;
  }
  std::vector<uint8_t> decrypted_data(encrypted_size, 0);
  if (!TpmDecrypt(
      resource_manager_->Esys(),
      key_slot->get(),
      TpmAuth(ESYS_TR_PASSWORD),
      encrypted_data.data(),
      decrypted_data.data(),
      encrypted_size)) {
    LOG(ERROR) << "Failed to decrypt encrypted data";
    return false;
  }
  auto decrypted_buf = decrypted_data.data();
  auto decrypted_buf_end = decrypted_data.data() + wrapped_size;
  if (!wrapped_->Deserialize(
      const_cast<const uint8_t **>(&decrypted_buf), decrypted_buf_end)) {
    LOG(ERROR) << "Failed to deserialize wrapped type";
    return false;
  }
  if (decrypted_buf != decrypted_buf_end) {
    LOG(ERROR) << "Inner type did not use all data";
    return false;
  }
  return true;
}

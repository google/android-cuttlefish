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

#include "hmac_serializable.h"

#include <android-base/logging.h>

#include "host/commands/secure_env/tpm_auth.h"
#include "host/commands/secure_env/tpm_hmac.h"

HmacSerializable::HmacSerializable(
    TpmResourceManager* resource_manager,
    std::function<TpmObjectSlot(TpmResourceManager*)> signing_key_fn,
    uint32_t digest_size,
    Serializable* wrapped) :
    resource_manager_(resource_manager),
    signing_key_fn_(signing_key_fn),
    digest_size_(digest_size),
    wrapped_(wrapped) {
}

size_t HmacSerializable::SerializedSize() const {
  auto digest_size = sizeof(uint32_t) + digest_size_;
  auto data_size = sizeof(uint32_t) + wrapped_->SerializedSize();
  return digest_size + data_size;
}

uint8_t* HmacSerializable::Serialize(uint8_t* buf, const uint8_t* end) const {
  auto wrapped_size = wrapped_->SerializedSize();
  buf = keymaster::append_uint32_to_buf(buf, end, wrapped_size);
  auto signed_data = buf;
  buf = wrapped_->Serialize(buf, end);
  if (buf - signed_data != wrapped_size) {
    LOG(ERROR) << "Serialized wrapped data did not match expected size.";
    return buf;
  }
  auto key = signing_key_fn_(resource_manager_);
  if (!key) {
    LOG(ERROR) << "Could not retrieve key";
    return buf;
  }
  auto hmac_data =
    TpmHmac(
        resource_manager_,
        key->get(),
        TpmAuth(ESYS_TR_PASSWORD),
        signed_data,
        wrapped_size);
  if (!hmac_data) {
    LOG(ERROR) << "Failed to produce hmac";
    return buf;
  }
  if (hmac_data->size != digest_size_) {
    LOG(ERROR) << "Unexpected digest size. Wanted " << digest_size_
               << ", TPM produced " << hmac_data->size;
    return buf;
  }
  return keymaster::append_size_and_data_to_buf(
      buf, end, hmac_data->buffer, digest_size_);
}

bool HmacSerializable::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
  size_t signed_data_size;
  keymaster::UniquePtr<uint8_t[]> signed_data;
  bool success =
      keymaster::copy_size_and_data_from_buf(
          buf_ptr, end, &signed_data_size, &signed_data);
  if (!success) {
    LOG(ERROR) << "Failed to retrieve signed data";
    return false;
  }
  size_t signature_size;
  keymaster::UniquePtr<uint8_t[]> signature;
  success =
      keymaster::copy_size_and_data_from_buf(
          buf_ptr, end, &signature_size, &signature);
  if (!success) {
    LOG(ERROR) << "Failed to retrieve signature";
    return false;
  }
  if (signature_size != digest_size_) {
    LOG(ERROR) << "Digest size did not match expected size.";
    return false;
  }
  auto key = signing_key_fn_(resource_manager_);
  if (!key) {
    LOG(ERROR) << "Could not retrieve key";
    return false;
  }
  auto hmac_check =
    TpmHmac(
        resource_manager_,
        key->get(),
        TpmAuth(ESYS_TR_PASSWORD),
        signed_data.get(),
        signed_data_size);
  if (!hmac_check) {
    LOG(ERROR) << "Unable to calculate signature check";
    return false;
  }
  if (hmac_check->size != digest_size_) {
    LOG(ERROR) << "Unexpected signature check size. Wanted " << digest_size_
               << ", TPM produced " << hmac_check->size;
    return false;
  }
  if (memcmp(signature.get(), hmac_check->buffer, digest_size_) != 0) {
    LOG(ERROR) << "Signature check did not match original signature.";
    return false;
  }
  // Now that we've validated integrity on the data, do the inner deserialization
  auto inner_buf = signed_data.get();
  auto inner_buf_end = inner_buf + signed_data_size;
  return wrapped_->Deserialize(
      const_cast<const uint8_t**>(&inner_buf), inner_buf_end);
}

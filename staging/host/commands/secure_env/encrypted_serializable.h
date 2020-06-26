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

#pragma once

#include <keymaster/serializable.h>

#include "host/commands/secure_env/tpm_resource_manager.h"

/**
 * A keymaster::Serializable that wraps another keymaster::Serializable,
 * encrypting the data with a TPM to ensure privacy.
 *
 * This implementation randomly generates a unique key which only exists inside
 * the TPM, and uses it to encrypt the data from the other Serializable
 * instance. The encrypted data, together with information about the unique key
 * is stored in the output data. The unique key information is something that
 * can only be decoded using a TPM, which will detect if the key is corrupted.
 * However, this implementation will not detect if the encrypted data is
 * corrupted, which could break the other Serializable instance on
 * deserialization. This class should be used with something else to verify
 * that the data hasn't been tampered with.
 *
 * The serialization format is:
 * [tpm key public data] [tpm key private data]
 * [uint32_t: block_size]
 * [uint32_t: encrypted_length] [encrypted_data]
 *
 * The actual length of [encrypted_data] in the serialized format is
 * [encrypted_length] rounded up to the nearest multiple of [block_size].
 * [encrypted_length] is the true length of the data before encryption, without
 * padding.
 */
class EncryptedSerializable : public keymaster::Serializable {
public:
  EncryptedSerializable(TpmResourceManager*,
                        std::function<TpmObjectSlot(TpmResourceManager*)>,
                        Serializable*);

  size_t SerializedSize() const override;
  uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
  bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;
private:
  TpmResourceManager* resource_manager_;
  std::function<TpmObjectSlot(TpmResourceManager*)> parent_key_fn_;
  keymaster::Serializable* wrapped_;
};

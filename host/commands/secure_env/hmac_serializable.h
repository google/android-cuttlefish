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
 * protecting it from tampering while it is stored elsewhere. This stores
 * the serialized data of the other type together with a signature over that
 * serialized data. When deserializing, it will attempt to make the same
 * signature over the data. If the signature or data has been tampered with,
 * the signatures won't match and it won't attempt to deserialize the wrapped
 * type.
 *
 * The serialization format is:
 * [uint32_t: wrapped_size] [wrapped_data]
 * [uint32_t: signature_size] [signature_data]
 *
 * While this class currently assumes all signatures will use the same key
 * and algorithm and therefore be the same size, the serialization format is
 * future-proof to accommodate signature changes.
 */
class HmacSerializable : public keymaster::Serializable {
public:
  HmacSerializable(TpmResourceManager*,
                   std::function<TpmObjectSlot(TpmResourceManager*)>,
                   uint32_t digest_size,
                   Serializable*);

  size_t SerializedSize() const override;
  uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
  bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;
private:
  TpmResourceManager* resource_manager_;
  std::function<TpmObjectSlot(TpmResourceManager*)> signing_key_fn_;
  uint32_t digest_size_;
  keymaster::Serializable* wrapped_;
};

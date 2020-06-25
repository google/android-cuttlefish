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

#include <cstddef>

#include "keymaster/serializable.h"
#include "tss2/tss2_mu.h"
#include "tss2/tss2_rc.h"
#include "tss2/tss2_tpm2_types.h"

#include <android-base/logging.h>

/**
 * An implementation of a keymaster::Serializable type that refers to a TPM type
 * by an unmanaged pointer. When the TpmSerializable serializes or deserializes
 * data, it loads it from and saves it to the pointed at instance.
 *
 * The serialization format is the same as the one used in the command protocol
 * for TPM messages.
 *
 * This is a template class, specialized in the corresponding implementation
 * file for the TPM types necessary to serialize as part of larger Keymaster
 * serializable types.
 */
template<typename Type>
class TpmSerializable : public keymaster::Serializable {
public:
  TpmSerializable(Type*);

  size_t SerializedSize() const override;
  uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
  bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;
private:
  Type* instance_;
};

using SerializeTpmKeyPrivate = TpmSerializable<TPM2B_PRIVATE>;
using SerializeTpmKeyPublic = TpmSerializable<TPM2B_PUBLIC>;

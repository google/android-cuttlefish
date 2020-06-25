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

#include "tpm_serialize.h"

#include <cstring>

#include <android-base/logging.h>
#include "tss2/tss2_mu.h"
#include "tss2/tss2_rc.h"

template<typename T>
int MarshalFn = 0; // Break code without an explicit specialization.

template<typename T>
int UnmarshalFn = 0; // Break code without an explicit specialization.

template<>
auto MarshalFn<TPM2B_PRIVATE> = Tss2_MU_TPM2B_PRIVATE_Marshal;

template<>
auto UnmarshalFn<TPM2B_PRIVATE> = Tss2_MU_TPM2B_PRIVATE_Unmarshal;

template<>
auto MarshalFn<TPM2B_PUBLIC> = Tss2_MU_TPM2B_PUBLIC_Marshal;

template<>
auto UnmarshalFn<TPM2B_PUBLIC> = Tss2_MU_TPM2B_PUBLIC_Unmarshal;

template<typename T>
TpmSerializable<T>::TpmSerializable(T* instance) : instance_(instance) {}

template<typename T>
size_t TpmSerializable<T>::SerializedSize() const {
  std::size_t size = 0;
  auto rc = MarshalFn<T>(instance_, nullptr, sizeof(T), &size);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "tss2 marshalling failed: " << Tss2_RC_Decode(rc)
                << "(" << rc << ")";
    return -1;
  }
  return size;
}

template<typename T>
uint8_t* TpmSerializable<T>::Serialize(uint8_t* buf, const uint8_t* end) const {
  std::size_t offset = 0;
  auto rc = MarshalFn<T>(instance_, buf, end - buf, &offset);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "tss2 marshalling failed: " << Tss2_RC_Decode(rc)
                << "(" << rc << ")";
    return buf;
  }
  return buf + offset;
}

template<typename T>
bool TpmSerializable<T>::Deserialize(
    const uint8_t** buf_ptr, const uint8_t* end) {
  std::size_t offset = 0;
  auto rc = UnmarshalFn<T>(*buf_ptr, end - *buf_ptr, &offset, instance_);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "tss2 unmarshalling failed: " << Tss2_RC_Decode(rc)
                << "(" << rc << ")";
    return false;
  }
  *buf_ptr += offset;
  return true;
}

template class TpmSerializable<TPM2B_PRIVATE>;
template class TpmSerializable<TPM2B_PUBLIC>;

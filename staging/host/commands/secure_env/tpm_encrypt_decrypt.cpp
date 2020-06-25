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

#include "tpm_encrypt_decrypt.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <android-base/logging.h>
#include <tss2/tss2_rc.h>

using keymaster::KeymasterBlob;

static bool TpmEncryptDecrypt(
    ESYS_CONTEXT* esys,
    ESYS_TR key_handle,
    TpmAuth auth,
    uint8_t* data_in,
    uint8_t* data_out,
    size_t data_size,
    bool decrypt) {
  // TODO(schuffelen): Pipeline this for performance. Will require reevaluating
  // the initialization vector logic.
  std::vector<unsigned char> converted(data_size);
  // malloc for parity with Esys_EncryptDecrypt2
  TPM2B_IV* init_vector_in = (TPM2B_IV*) malloc(sizeof(TPM2B_IV));
  *init_vector_in = {};
  init_vector_in->size = 16;
  for (auto processed = 0; processed < data_size;) {
    TPM2B_MAX_BUFFER in_data;
    in_data.size =
        std::min(data_size - processed, sizeof(in_data.buffer));
    std::memcpy(in_data.buffer, &data_in[processed], in_data.size);
    TPM2B_IV* init_vector_out = nullptr;
    TPM2B_MAX_BUFFER* out_data = nullptr;
    auto rc = Esys_EncryptDecrypt2(
        esys,
        key_handle,
        auth.auth1(),
        auth.auth2(),
        auth.auth3(),
        &in_data,
        decrypt ? TPM2_YES : TPM2_NO,
        TPM2_ALG_NULL,
        init_vector_in,
        &out_data,
        &init_vector_out);
    if (rc != TPM2_RC_SUCCESS) {
      LOG(ERROR) << "Esys_EncryptDecrypt2 failed: " << Tss2_RC_Decode(rc)
                 << "(" << rc << ")";
      Esys_Free(init_vector_in);
      return false;
    }
    CHECK(init_vector_out != nullptr) << "init_vector_out was NULL";
    CHECK(out_data != nullptr) << "out_data was NULL";
    CHECK(out_data->size == in_data.size) << "data size mismatch";
    std::memcpy(&data_out[processed], out_data->buffer, out_data->size);
    Esys_Free(out_data);
    Esys_Free(init_vector_in);
    init_vector_in = init_vector_out;
    processed += in_data.size;
  }
  Esys_Free(init_vector_in);
  return true;
}

bool TpmEncrypt(
    ESYS_CONTEXT* esys,
    ESYS_TR key_handle,
    TpmAuth auth,
    uint8_t* data_in,
    uint8_t* data_out,
    size_t data_size) {
  return TpmEncryptDecrypt(
      esys, key_handle, auth, data_in, data_out, data_size, false);
}

bool TpmDecrypt(
    ESYS_CONTEXT* esys,
    ESYS_TR key_handle,
    TpmAuth auth,
    uint8_t* data_in,
    uint8_t* data_out,
    size_t data_size) {
  return TpmEncryptDecrypt(
      esys, key_handle, auth, data_in, data_out, data_size, true);
}

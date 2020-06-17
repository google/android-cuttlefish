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

#include "tpm_random_source.h"

#include <android-base/logging.h>
#include "tss2/tss2_esys.h"
#include "tss2/tss2_rc.h"

TpmRandomSource::TpmRandomSource(ESYS_CONTEXT* esys) : esys_(esys) {
}

keymaster_error_t TpmRandomSource::GenerateRandom(
    uint8_t* random, size_t requested_length) const {
  if (requested_length == 0) {
    return KM_ERROR_OK;
  }
  // TODO(b/158790549): Pipeline these calls.
  TPM2B_DIGEST* generated = nullptr;
  while (requested_length > sizeof(generated->buffer)) {
    auto rc = Esys_GetRandom(esys_, ESYS_TR_NONE, ESYS_TR_NONE,
                             ESYS_TR_NONE, sizeof(generated->buffer),
                             &generated);
    if (rc != TSS2_RC_SUCCESS) {
      LOG(ERROR) << "Esys_GetRandom failed with " << rc << " ("
                 << Tss2_RC_Decode(rc) << ")";
      // TODO(b/158790404): Return a better error code.
      return KM_ERROR_UNKNOWN_ERROR;
    }
    memcpy(random, generated->buffer, sizeof(generated->buffer));
    random = (uint8_t*) random + sizeof(generated->buffer);
    requested_length -= sizeof(generated->buffer);
    Esys_Free(generated);
  }
  auto rc = Esys_GetRandom(esys_, ESYS_TR_NONE, ESYS_TR_NONE,
                           ESYS_TR_NONE, requested_length, &generated);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_GetRandom failed with " << rc << " ("
                << Tss2_RC_Decode(rc) << ")";
    // TODO(b/158790404): Return a better error code.
    return KM_ERROR_UNKNOWN_ERROR;
  }
  memcpy(random, generated->buffer, requested_length);
  Esys_Free(generated);
  return KM_ERROR_OK;
}

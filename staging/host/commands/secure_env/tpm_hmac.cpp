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

#include "tpm_hmac.h"

#include <android-base/logging.h>
#include <tss2/tss2_rc.h>

#include "host/commands/secure_env/tpm_resource_manager.h"

/* For data large enough to fit in a single TPM2_HMAC call. */
static UniqueEsysPtr<TPM2B_DIGEST> OneshotHmac(
    TpmResourceManager* resource_manager,
    ESYS_TR key_handle,
    TpmAuth auth,
    const uint8_t* data,
    size_t data_size) {
  if (data_size  > TPM2_MAX_DIGEST_BUFFER) {
    LOG(ERROR) << "Logic error: OneshotSign called with data_size "
               << data_size << " (> " << TPM2_MAX_DIGEST_BUFFER << ")";
    return {};
  }
  TPM2B_MAX_BUFFER buffer;
  static_assert(sizeof(buffer.buffer) >= TPM2_MAX_DIGEST_BUFFER);
  buffer.size = data_size;
  memcpy(buffer.buffer, data, data_size);
  TPM2B_DIGEST* out_hmac = nullptr;
  auto rc = Esys_HMAC(
      resource_manager->Esys(),
      key_handle,
      auth.auth1(),
      auth.auth2(),
      auth.auth3(),
      &buffer,
      TPM2_ALG_NULL,
      &out_hmac);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "TPM2_HMAC failed: " << Tss2_RC_Decode(rc) << "(" << rc << ")";
    return {};
  }
  if (out_hmac == nullptr) {
    LOG(ERROR) << "out_hmac unset";
    return {};
  }
  return UniqueEsysPtr<TPM2B_DIGEST>(out_hmac);
}

/* For data too large to fit in a single TPM2_HMAC call. */
static UniqueEsysPtr<TPM2B_DIGEST> SegmentedHmac(
    TpmResourceManager* resource_manager,
    ESYS_TR key_handle,
    TpmAuth key_auth,
    const uint8_t* data,
    size_t data_size) {
  // TODO(schuffelen): Pipeline commands where possible.
  TPM2B_AUTH sequence_auth;
  sequence_auth.size = sizeof(rand());
  *reinterpret_cast<decltype(rand())*>(sequence_auth.buffer) = rand();
  ESYS_TR sequence_handle;
  auto slot = resource_manager->ReserveSlot();
  if (!slot) {
    LOG(ERROR) << "No slots available";
    return {};
  }
  auto rc = Esys_HMAC_Start(
      resource_manager->Esys(),
      key_handle,
      key_auth.auth1(),
      key_auth.auth2(),
      key_auth.auth3(),
      &sequence_auth,
      TPM2_ALG_NULL,
      &sequence_handle);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "TPM2_HMAC_Start failed: " << Tss2_RC_Decode(rc)
               << "(" << rc << ")";
    return {};
  }
  slot->set(sequence_handle);
  rc = Esys_TR_SetAuth(
      resource_manager->Esys(), sequence_handle, &sequence_auth);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_TR_SetAuth failed: " << Tss2_RC_Decode(rc)
               << "(" << rc << ")";
    return {};
  }
  auto hashed = 0;
  TPM2B_MAX_BUFFER buffer;
  while (data_size - hashed > TPM2_MAX_DIGEST_BUFFER) {
    buffer.size = TPM2_MAX_DIGEST_BUFFER;
    memcpy(buffer.buffer, &data[hashed], TPM2_MAX_DIGEST_BUFFER);
    hashed += TPM2_MAX_DIGEST_BUFFER;
    rc = Esys_SequenceUpdate(
        resource_manager->Esys(),
        sequence_handle,
        ESYS_TR_PASSWORD,
        ESYS_TR_NONE,
        ESYS_TR_NONE,
        &buffer);
    if (rc != TPM2_RC_SUCCESS) {
      LOG(ERROR) << "Esys_SequenceUpdate failed: " << Tss2_RC_Decode(rc)
                << "(" << rc << ")";
      return {};
    }
  }
  buffer.size = data_size - hashed;
  memcpy(buffer.buffer, &data[hashed], buffer.size);
  TPM2B_DIGEST* out_hmac = nullptr;
  TPMT_TK_HASHCHECK* validation = nullptr;
  rc = Esys_SequenceComplete(
      resource_manager->Esys(),
      sequence_handle,
      ESYS_TR_PASSWORD,
      ESYS_TR_NONE,
      ESYS_TR_NONE,
      &buffer,
      ESYS_TR_RH_NULL,
      &out_hmac,
      &validation);
  if (rc != TPM2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_SequenceComplete failed: " << Tss2_RC_Decode(rc)
               << "(" << rc << ")";
    return {};
  }
  // TPM2_SequenceComplete already flushes the sequence context on success.
  slot->set(ESYS_TR_NONE);
  if (out_hmac == nullptr) {
    LOG(ERROR) << "out_hmac was null";
    return {};
  }
  Esys_Free(validation);
  return UniqueEsysPtr<TPM2B_DIGEST>(out_hmac);
}

UniqueEsysPtr<TPM2B_DIGEST> TpmHmac(
    TpmResourceManager* resource_manager,
    ESYS_TR key_handle,
    TpmAuth auth,
    const uint8_t* data,
    size_t data_size) {
  auto fn = data_size > TPM2_MAX_DIGEST_BUFFER ? SegmentedHmac : OneshotHmac;
  return fn(resource_manager, key_handle, auth, data, data_size);
}

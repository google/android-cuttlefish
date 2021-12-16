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

#include "host/commands/secure_env/tpm_attestation_record.h"

#include <keymaster/contexts/soft_attestation_cert.h>
#include <keymaster/km_openssl/attestation_record.h>

#include <openssl/rand.h>

#include <android-base/logging.h>

namespace cuttlefish {

namespace {
using VerifiedBootParams = keymaster::AttestationContext::VerifiedBootParams;
using keymaster::AuthorizationSet;

VerifiedBootParams MakeVbParams() {
  // Cuttlefish is hard-coded to verifiedbootstate=orange
  // See device/google/cuttlefish/host/libs/config/bootconfig_args.cpp
  VerifiedBootParams vb_params;
  static uint8_t empty_vb_key[32] = {};
  vb_params.verified_boot_key = {empty_vb_key, sizeof(empty_vb_key)};
  vb_params.verified_boot_hash = {empty_vb_key, sizeof(empty_vb_key)};
  vb_params.verified_boot_state = KM_VERIFIED_BOOT_UNVERIFIED;
  vb_params.device_locked = false;
  return vb_params;
}

}  // namespace

TpmAttestationRecordContext::TpmAttestationRecordContext()
    : keymaster::AttestationContext(::keymaster::KmVersion::KEYMINT_2),
      vb_params_(MakeVbParams()),
      unique_id_hbk_(16) {
  RAND_bytes(unique_id_hbk_.data(), unique_id_hbk_.size());
}

keymaster_security_level_t TpmAttestationRecordContext::GetSecurityLevel() const {
  return KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT;
}

keymaster_error_t TpmAttestationRecordContext::VerifyAndCopyDeviceIds(
    const AuthorizationSet& /*attestation_params*/,
    AuthorizationSet* /*attestation*/) const {
  LOG(DEBUG) << "TODO(schuffelen): Implement VerifyAndCopyDeviceIds";
  return KM_ERROR_UNIMPLEMENTED;
}

keymaster::Buffer TpmAttestationRecordContext::GenerateUniqueId(
    uint64_t creation_date_time, const keymaster_blob_t& application_id,
    bool reset_since_rotation, keymaster_error_t* error) const {
  *error = KM_ERROR_OK;
  return keymaster::generate_unique_id(unique_id_hbk_, creation_date_time,
                                       application_id, reset_since_rotation);
}

const VerifiedBootParams* TpmAttestationRecordContext::GetVerifiedBootParams(
    keymaster_error_t* error) const {
  static VerifiedBootParams vb_params = MakeVbParams();
  *error = KM_ERROR_OK;
  return &vb_params;
}

keymaster::KeymasterKeyBlob
TpmAttestationRecordContext::GetAttestationKey(keymaster_algorithm_t algorithm,
                                               keymaster_error_t* error) const {
  return keymaster::KeymasterKeyBlob(*keymaster::getAttestationKey(algorithm, error));
}

keymaster::CertificateChain
TpmAttestationRecordContext::GetAttestationChain(keymaster_algorithm_t algorithm,
                                                 keymaster_error_t* error) const {
  return keymaster::getAttestationChain(algorithm, error);
}

}  // namespace cuttlefish

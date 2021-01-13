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

#include <android-base/logging.h>

using keymaster::AuthorizationSet;

keymaster_security_level_t TpmAttestationRecordContext::GetSecurityLevel() const {
  return KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT;
}

keymaster_error_t TpmAttestationRecordContext::VerifyAndCopyDeviceIds(
    const AuthorizationSet& attestation_params,
    AuthorizationSet* attestation) const {
  LOG(DEBUG) << "TODO(schuffelen): Implement VerifyAndCopyDeviceIds";
  attestation->Difference(attestation_params);
  attestation->Union(attestation_params);
  if (int index = attestation->find(keymaster::TAG_ATTESTATION_APPLICATION_ID)) {
    attestation->erase(index);
  }
  return KM_ERROR_OK;
}

keymaster::Buffer TpmAttestationRecordContext::GenerateUniqueId(
    uint64_t, const keymaster_blob_t&, bool, keymaster_error_t* error) const {
  LOG(ERROR) << "TODO(schuffelen): Implement GenerateUniqueId";
  *error = KM_ERROR_UNIMPLEMENTED;
  return {};
}

const keymaster::AttestationContext::VerifiedBootParams*
TpmAttestationRecordContext::GetVerifiedBootParams(keymaster_error_t* error) const {
  LOG(DEBUG) << "TODO(schuffelen): Implement GetVerifiedBootParams";
  if (!vb_params_) {
      vb_params_.reset(new VerifiedBootParams{});

      // TODO(schuffelen): Get this data out of vbmeta
      static uint8_t fake_vb_key[32];
      static bool fake_vb_key_initialized = false;
      if (!fake_vb_key_initialized) {
        for (int i = 0; i < sizeof(fake_vb_key); i++) {
          fake_vb_key[i] = rand();
        }
        fake_vb_key_initialized = true;
      }
      vb_params_->verified_boot_key = {fake_vb_key, sizeof(fake_vb_key)};
      vb_params_->verified_boot_hash = {fake_vb_key, sizeof(fake_vb_key)};
      vb_params_->verified_boot_state = KM_VERIFIED_BOOT_VERIFIED;
      vb_params_->device_locked = true;
  }
  *error = KM_ERROR_OK;
  return vb_params_.get();
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

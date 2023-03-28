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
  VerifiedBootParams vb_params;

  // TODO: If Cuttlefish ever supports a boot state other than "orange", we'll
  // also need to plumb in the public key.
  static uint8_t empty_vb_key[32] = {};
  vb_params.verified_boot_key = {empty_vb_key, sizeof(empty_vb_key)};
  vb_params.verified_boot_hash = {empty_vb_key, sizeof(empty_vb_key)};
  vb_params.verified_boot_state = KM_VERIFIED_BOOT_UNVERIFIED;
  vb_params.device_locked = false;
  return vb_params;
}

}  // namespace

TpmAttestationRecordContext::TpmAttestationRecordContext()
    : keymaster::AttestationContext(::keymaster::KmVersion::KEYMINT_3),
      vb_params_(MakeVbParams()),
      unique_id_hbk_(16) {
  RAND_bytes(unique_id_hbk_.data(), unique_id_hbk_.size());
}

keymaster_security_level_t TpmAttestationRecordContext::GetSecurityLevel() const {
  return KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT;
}

// Return true if entries match, false otherwise.
bool matchAttestationId(keymaster_blob_t blob, const std::vector<uint8_t>& id) {
  if (blob.data_length != id.size()) {
    return false;
  }
  if (memcmp(blob.data, id.data(), id.size())) {
    return false;
  }
  return true;
}

keymaster_error_t TpmAttestationRecordContext::VerifyAndCopyDeviceIds(
    const AuthorizationSet& attestation_params,
    AuthorizationSet* attestation) const {
  const AttestationIds& ids = attestation_ids_;
  bool found_mismatch = false;
  for (auto& entry : attestation_params) {
    switch (entry.tag) {
      case KM_TAG_ATTESTATION_ID_BRAND:
        found_mismatch |= !matchAttestationId(entry.blob, ids.brand);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_DEVICE:
        found_mismatch |= !matchAttestationId(entry.blob, ids.device);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_PRODUCT:
        found_mismatch |= !matchAttestationId(entry.blob, ids.product);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_SERIAL:
        found_mismatch |= !matchAttestationId(entry.blob, ids.serial);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_IMEI:
        found_mismatch |= !matchAttestationId(entry.blob, ids.imei);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_MEID:
        found_mismatch |= !matchAttestationId(entry.blob, ids.meid);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_MANUFACTURER:
        found_mismatch |= !matchAttestationId(entry.blob, ids.manufacturer);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_MODEL:
        found_mismatch |= !matchAttestationId(entry.blob, ids.model);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_SECOND_IMEI:
        found_mismatch |= !matchAttestationId(entry.blob, ids.second_imei);
        attestation->push_back(entry);
        break;

      default:
        // Ignore non-ID tags.
        break;
    }
  }

  if (found_mismatch) {
    attestation->Clear();
    return KM_ERROR_CANNOT_ATTEST_IDS;
  }

  return KM_ERROR_OK;
}

keymaster::Buffer TpmAttestationRecordContext::GenerateUniqueId(
    uint64_t creation_date_time, const keymaster_blob_t& application_id,
    bool reset_since_rotation, keymaster_error_t* error) const {
  keymaster::Buffer unique_id;
  *error = keymaster::generate_unique_id(unique_id_hbk_, creation_date_time,
                                         application_id, reset_since_rotation,
                                         &unique_id);
  return unique_id;
}

const VerifiedBootParams* TpmAttestationRecordContext::GetVerifiedBootParams(
    keymaster_error_t* error) const {
  *error = KM_ERROR_OK;
  return &vb_params_;
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

void TpmAttestationRecordContext::SetVerifiedBootInfo(
    std::string_view verified_boot_state, std::string_view bootloader_state,
    const std::vector<uint8_t>& vbmeta_digest) {
  vbmeta_digest_ = vbmeta_digest;
  vb_params_.verified_boot_hash = {vbmeta_digest_.data(),
                                   vbmeta_digest_.size()};

  if (verified_boot_state == "green") {
    vb_params_.verified_boot_state = KM_VERIFIED_BOOT_VERIFIED;
  } else if (verified_boot_state == "yellow") {
    vb_params_.verified_boot_state = KM_VERIFIED_BOOT_SELF_SIGNED;
  } else if (verified_boot_state == "red") {
    vb_params_.verified_boot_state = KM_VERIFIED_BOOT_FAILED;
  } else {  // Default to orange
    vb_params_.verified_boot_state = KM_VERIFIED_BOOT_UNVERIFIED;
  }

  vb_params_.device_locked = bootloader_state == "locked";
}

keymaster_error_t TpmAttestationRecordContext::SetAttestationIds(
    const keymaster::SetAttestationIdsRequest& request) {
  attestation_ids_.brand.assign(request.brand.begin(), request.brand.end());
  attestation_ids_.device.assign(request.device.begin(), request.device.end());
  attestation_ids_.product.assign(request.product.begin(),
                                  request.product.end());
  attestation_ids_.serial.assign(request.serial.begin(), request.serial.end());
  attestation_ids_.imei.assign(request.imei.begin(), request.imei.end());
  attestation_ids_.meid.assign(request.meid.begin(), request.meid.end());
  attestation_ids_.manufacturer.assign(request.manufacturer.begin(),
                                       request.manufacturer.end());
  attestation_ids_.model.assign(request.model.begin(), request.model.end());

  return KM_ERROR_OK;
}

keymaster_error_t TpmAttestationRecordContext::SetAttestationIdsKM3(
    const keymaster::SetAttestationIdsKM3Request& request) {
  SetAttestationIds(request.base);
  attestation_ids_.second_imei.assign(request.second_imei.begin(),
                                      request.second_imei.end());

  return KM_ERROR_OK;
}
}  // namespace cuttlefish

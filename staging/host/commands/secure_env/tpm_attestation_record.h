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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <keymaster/android_keymaster_messages.h>
#include <keymaster/attestation_context.h>

namespace cuttlefish {

struct AttestationIds {
  std::vector<uint8_t> brand;
  std::vector<uint8_t> device;
  std::vector<uint8_t> product;
  std::vector<uint8_t> serial;
  std::vector<uint8_t> imei;
  std::vector<uint8_t> meid;
  std::vector<uint8_t> manufacturer;
  std::vector<uint8_t> model;
  std::vector<uint8_t> second_imei;
};

class TpmAttestationRecordContext : public keymaster::AttestationContext {
public:
 TpmAttestationRecordContext();
 ~TpmAttestationRecordContext() = default;

 keymaster_security_level_t GetSecurityLevel() const override;
 keymaster_error_t VerifyAndCopyDeviceIds(
     const keymaster::AuthorizationSet&,
     keymaster::AuthorizationSet*) const override;
 keymaster::Buffer GenerateUniqueId(uint64_t, const keymaster_blob_t&, bool,
                                    keymaster_error_t*) const override;
 const VerifiedBootParams* GetVerifiedBootParams(
     keymaster_error_t* error) const override;
 keymaster::KeymasterKeyBlob GetAttestationKey(
     keymaster_algorithm_t algorithm, keymaster_error_t* error) const override;
 keymaster::CertificateChain GetAttestationChain(
     keymaster_algorithm_t algorithm, keymaster_error_t* error) const override;
 void SetVerifiedBootInfo(std::string_view verified_boot_state,
                          std::string_view bootloader_state,
                          const std::vector<uint8_t>& vbmeta_digest);
 keymaster_error_t SetAttestationIds(
     const keymaster::SetAttestationIdsRequest& request);
 keymaster_error_t SetAttestationIdsKM3(
     const keymaster::SetAttestationIdsKM3Request& request);

private:
 std::vector<uint8_t> vbmeta_digest_;
 VerifiedBootParams vb_params_;
 std::vector<uint8_t> unique_id_hbk_;
 AttestationIds attestation_ids_;
};

}  // namespace cuttlefish

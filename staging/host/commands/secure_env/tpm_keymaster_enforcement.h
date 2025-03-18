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

#include <keymaster/keymaster_enforcement.h>

#include "host/commands/secure_env/tpm_gatekeeper.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {

/**
 * Implementation of keymaster::KeymasterEnforcement that depends on having a
 * TPM available. See the definitions in
 * system/keymaster/include/keymaster/keymaster_enforcement.h
 */
class TpmKeymasterEnforcement : public keymaster::KeymasterEnforcement {
 public:
  TpmKeymasterEnforcement(TpmResourceManager& resource_manager,
                          TpmGatekeeper& gatekeeper);
  ~TpmKeymasterEnforcement();

  bool activation_date_valid(uint64_t activation_date) const override;
  bool expiration_date_passed(uint64_t expiration_date) const override;
  bool auth_token_timed_out(const hw_auth_token_t& token,
                            uint32_t timeout) const override;
  uint64_t get_current_time_ms() const override;

  keymaster_security_level_t SecurityLevel() const override;
  bool ValidateTokenSignature(const hw_auth_token_t& token) const override;

  keymaster_error_t GetHmacSharingParameters(
      keymaster::HmacSharingParameters* params) override;
  keymaster_error_t ComputeSharedHmac(
      const keymaster::HmacSharingParametersArray& params_array,
      keymaster::KeymasterBlob* sharingCheck) override;

  keymaster::VerifyAuthorizationResponse VerifyAuthorization(
      const keymaster::VerifyAuthorizationRequest& request) override;

  keymaster_error_t GenerateTimestampToken(
      keymaster::TimestampToken* token) override;

  keymaster::KmErrorOr<std::array<uint8_t, 32>> ComputeHmac(
      const std::vector<uint8_t>& data_to_mac) const override;

  bool CreateKeyId(const keymaster_key_blob_t& key_blob,
                   keymaster::km_id_t* keyid) const override;

 private:
  TpmResourceManager& resource_manager_;
  TpmGatekeeper& gatekeeper_;
  bool have_saved_params_ = false;
  keymaster::HmacSharingParameters saved_params_;
};

}  // namespace cuttlefish

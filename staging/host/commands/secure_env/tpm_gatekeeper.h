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

#include "gatekeeper/gatekeeper.h"
#include "tss2/tss2_esys.h"

#include "host/commands/secure_env/gatekeeper_storage.h"
#include "host/commands/secure_env/storage/storage.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {

/**
 * See method descriptions for this class in
 * system/gatekeeper/include/gatekeeper/gatekeeper.h
 */
class TpmGatekeeper : public gatekeeper::GateKeeper {
public:
  TpmGatekeeper(
      TpmResourceManager& resource_manager,
      secure_env::Storage& secure_storage,
      secure_env::Storage& insecure_storage);

  bool GetAuthTokenKey(
      const uint8_t** auth_token_key, uint32_t* length) const override;

  void GetPasswordKey(const uint8_t** pasword_key, uint32_t* length) override;

  void ComputePasswordSignature(
      uint8_t* signature,
      uint32_t signature_length,
      const uint8_t* key,
      uint32_t key_length,
      const uint8_t* password,
      uint32_t password_length,
      gatekeeper::salt_t salt) const override;

  void GetRandom(void* random, uint32_t requested_size) const override;

  void ComputeSignature(
      uint8_t* signature,
      uint32_t signature_length,
      const uint8_t* key,
      uint32_t key_length,
      const uint8_t* message,
      uint32_t length) const override;

  uint64_t GetMillisecondsSinceBoot() const override;

  /**
   * Retrieves the failure record for user `uid`, assuming a user secret value
   * of `user_id`. If the secret value `user_id` is incorrect, the original
   * secret `user_id` value will be lost and cannot be recovered.
   */
  bool GetFailureRecord(
      uint32_t uid,
      gatekeeper::secure_id_t user_id,
      gatekeeper::failure_record_t *record,
      bool secure) override;

  bool ClearFailureRecord(
      uint32_t uid, gatekeeper::secure_id_t user_id, bool secure) override;

  bool WriteFailureRecord(
      uint32_t uid, gatekeeper::failure_record_t *record, bool secure) override;

  bool IsHardwareBacked() const override;
private:
  TpmResourceManager& resource_manager_;
  secure_env::Storage& secure_storage_;
  secure_env::Storage& insecure_storage_;
};

}  // namespace cuttlefish

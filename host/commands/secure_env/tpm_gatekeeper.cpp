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

#include "tpm_gatekeeper.h"

#include <algorithm>
#include <vector>

#include <android-base/logging.h>
#include <tss2/tss2_esys.h>
#include <tss2/tss2_mu.h>
#include <tss2/tss2_rc.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_auth.h"
#include "host/commands/secure_env/tpm_hmac.h"

TpmGatekeeper::TpmGatekeeper(TpmResourceManager* resource_manager)
    : resource_manager_(resource_manager)
    , random_source_(resource_manager->Esys()) {
}

/*
 * The reinterpret_cast and kPasswordUnique data is combined together with TPM
 * internal state to create the actual key used for Gatekeeper operations.
 */

bool TpmGatekeeper::GetAuthTokenKey(
    const uint8_t** auth_token_key, uint32_t* length) const {
  static constexpr char kAuthTokenUnique[] = "TpmGatekeeper auth token key";
  *auth_token_key = reinterpret_cast<const uint8_t*>(kAuthTokenUnique);
  *length = sizeof(kAuthTokenUnique);
  return true;
}

void TpmGatekeeper::GetPasswordKey(
    const uint8_t** password_key, uint32_t* length) {
  static constexpr char kPasswordUnique[] = "TpmGatekeeper password key";
  *password_key = reinterpret_cast<const uint8_t*>(kPasswordUnique);
  *length = sizeof(kPasswordUnique);
}

void TpmGatekeeper::ComputePasswordSignature(
    uint8_t* signature,
    uint32_t signature_length,
    const uint8_t* key,
    uint32_t key_length,
    const uint8_t* password,
    uint32_t password_length,
    gatekeeper::salt_t salt) const {
  std::vector<uint8_t> message(password_length + sizeof(salt));
  memcpy(message.data(), password, password_length);
  memcpy(message.data() + password_length, &salt, sizeof(salt));
  return ComputeSignature(
      signature,
      signature_length,
      key,
      key_length,
      message.data(),
      message.size());
}

void TpmGatekeeper::GetRandom(void* random, uint32_t requested_size) const {
  auto random_uint8 = reinterpret_cast<uint8_t*>(random);
  random_source_.GenerateRandom(random_uint8, requested_size);
}

void TpmGatekeeper::ComputeSignature(
    uint8_t* signature,
    uint32_t signature_length,
    const uint8_t* key,
    uint32_t key_length,
    const uint8_t* message,
    uint32_t length) const {
  memset(signature, 0, signature_length);
  std::string key_unique(reinterpret_cast<const char*>(key), key_length);
  PrimaryKeyBuilder key_builder;
  key_builder.UniqueData(key_unique);
  key_builder.SigningKey();
  auto key_slot = key_builder.CreateKey(resource_manager_);
  if (!key_slot) {
    LOG(ERROR) << "Unable to load signing key into TPM memory";
    return;
  }
  auto calculated_signature =
      TpmHmac(
          resource_manager_,
          key_slot->get(),
          TpmAuth(ESYS_TR_PASSWORD),
          message,
          length);
  if (!calculated_signature) {
    LOG(ERROR) << "Failure in calculating signature";
    return;
  }
  memcpy(
      signature,
      calculated_signature->buffer,
      std::min((int) calculated_signature->size, (int) signature_length));
}

uint64_t TpmGatekeeper::GetMillisecondsSinceBoot() const {
  struct timespec time;
  int res = clock_gettime(CLOCK_BOOTTIME, &time);
  if (res < 0) return 0;
  return (time.tv_sec * 1000) + (time.tv_nsec / 1000 / 1000);
}

bool TpmGatekeeper::GetFailureRecord(
    uint32_t uid,
    gatekeeper::secure_id_t user_id,
    gatekeeper::failure_record_t *record,
    bool secure) {
  (void) uid;
  (void) user_id;
  (void) record;
  (void) secure;
  return false;
}

bool TpmGatekeeper::ClearFailureRecord(
    uint32_t uid, gatekeeper::secure_id_t user_id, bool secure) {
  (void) uid;
  (void) user_id;
  (void) secure;
  return false;
}

bool TpmGatekeeper::WriteFailureRecord(
    uint32_t uid, gatekeeper::failure_record_t *record, bool secure) {
  (void) uid;
  (void) record;
  (void) secure;
  return false;
}

bool TpmGatekeeper::IsHardwareBacked() const {
  return true;
}


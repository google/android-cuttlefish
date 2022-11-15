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
#include <optional>
#include <vector>

#include <android-base/logging.h>
#include <tss2/tss2_esys.h>
#include <tss2/tss2_mu.h>
#include <tss2/tss2_rc.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_auth.h"
#include "host/commands/secure_env/tpm_hmac.h"
#include "host/commands/secure_env/tpm_random_source.h"

namespace cuttlefish {

TpmGatekeeper::TpmGatekeeper(
    TpmResourceManager& resource_manager,
    GatekeeperStorage& secure_storage,
    GatekeeperStorage& insecure_storage)
    : resource_manager_(resource_manager)
    , secure_storage_(secure_storage)
    , insecure_storage_(insecure_storage) {
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
  TpmRandomSource(resource_manager_.Esys())
      .GenerateRandom(random_uint8, requested_size);
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

  auto calculated_signature =
      TpmHmacWithContext(resource_manager_, key_unique, message, length);
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

gatekeeper::failure_record_t DefaultRecord(
    gatekeeper::secure_id_t secure_user_id) {
  return (gatekeeper::failure_record_t) {
    .secure_user_id = secure_user_id,
    .last_checked_timestamp = 0,
    .failure_counter = 0,
  };
}

static std::unique_ptr<TPM2B_MAX_NV_BUFFER> RecordToNvBuffer(
    const gatekeeper::failure_record_t& record) {
  auto ret = std::make_unique<TPM2B_MAX_NV_BUFFER>();
  static_assert(sizeof(ret->buffer) >= sizeof(record));
  ret->size = sizeof(record);
  std::memcpy(ret->buffer, &record, sizeof(record));
  return ret;
}

static std::optional<gatekeeper::failure_record_t> NvBufferToRecord(
    const TPM2B_MAX_NV_BUFFER& buffer) {
  gatekeeper::failure_record_t ret;
  if (buffer.size != sizeof(ret)) {
    LOG(ERROR) << "NV Buffer had an incorrect size.";
    return {};
  }
  memcpy(&ret, buffer.buffer, sizeof(ret));
  return ret;
}

static bool GetFailureRecordImpl(
    GatekeeperStorage& storage,
    uint32_t uid,
    gatekeeper::secure_id_t secure_user_id,
    gatekeeper::failure_record_t *record) {
  Json::Value key{std::to_string(uid)}; // jsoncpp integer comparisons are janky
  if (!storage.HasKey(key)) {
    if (!storage.Allocate(key, sizeof(gatekeeper::failure_record_t))) {
      LOG(ERROR) << "Allocation failed for user " << uid;
      return false;
    }
    auto buf = RecordToNvBuffer(DefaultRecord(secure_user_id));
    if (!storage.Write(key, *buf)) {
      LOG(ERROR) << "Failed to write record for " << uid;
      return false;
    }
  }
  auto record_read = storage.Read(key);
  if (!record_read) {
    LOG(ERROR) << "Failed to read record for " << uid;
    return false;
  }
  auto record_decoded = NvBufferToRecord(*record_read);
  if (!record_decoded) {
    LOG(ERROR) << "Failed to deserialize record for " << uid;
    return false;
  }
  if (record_decoded->secure_user_id == secure_user_id) {
    *record = *record_decoded;
    return true;
  }
  LOG(DEBUG) << "User id mismatch for " << uid;
  auto buf = RecordToNvBuffer(DefaultRecord(secure_user_id));
  if (!storage.Write(key, *buf)) {
    LOG(ERROR) << "Failed to write record for " << uid;
    return false;
  }
  *record = DefaultRecord(secure_user_id);
  return true;
}

bool TpmGatekeeper::GetFailureRecord(
    uint32_t uid,
    gatekeeper::secure_id_t secure_user_id,
    gatekeeper::failure_record_t *record,
    bool secure) {
  GatekeeperStorage& storage = secure ? secure_storage_ : insecure_storage_;
  return GetFailureRecordImpl(storage, uid, secure_user_id, record);
}

static bool WriteFailureRecordImpl(
    GatekeeperStorage& storage,
    uint32_t uid,
    gatekeeper::failure_record_t* record) {
  Json::Value key{std::to_string(uid)}; // jsoncpp integer comparisons are janky
  if (!storage.HasKey(key)) {
    if (!storage.Allocate(key, sizeof(gatekeeper::failure_record_t))) {
      LOG(ERROR) << "Allocation failed for user " << uid;
      return false;
    }
  }
  auto buf = RecordToNvBuffer(*record);
  if (!storage.Write(key, *buf)) {
    LOG(ERROR) << "Failed to write record for " << uid;
    return false;
  }
  return true;
}

bool TpmGatekeeper::ClearFailureRecord(
    uint32_t uid, gatekeeper::secure_id_t secure_user_id, bool secure) {
  GatekeeperStorage& storage = secure ? secure_storage_ : insecure_storage_;
  gatekeeper::failure_record_t record = DefaultRecord(secure_user_id);
  return WriteFailureRecordImpl(storage, uid, &record);
}

bool TpmGatekeeper::WriteFailureRecord(
    uint32_t uid, gatekeeper::failure_record_t *record, bool secure) {
  GatekeeperStorage& storage = secure ? secure_storage_ : insecure_storage_;
  return WriteFailureRecordImpl(storage, uid, record);
}

bool TpmGatekeeper::IsHardwareBacked() const {
  return true;
}

}  // namespace cuttlefish

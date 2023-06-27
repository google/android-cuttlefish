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

#ifdef _WIN32
#include <sysinfoapi.h>
#endif

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_auth.h"
#include "host/commands/secure_env/tpm_hmac.h"
#include "host/commands/secure_env/tpm_random_source.h"

namespace cuttlefish {

TpmGatekeeper::TpmGatekeeper(
    TpmResourceManager& resource_manager,
    secure_env::Storage& secure_storage,
    secure_env::Storage& insecure_storage)
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
#ifdef _WIN32
  return GetTickCount64();
#else
  struct timespec time;
  int res = clock_gettime(CLOCK_BOOTTIME, &time);
  if (res < 0) return 0;
  return (time.tv_sec * 1000) + (time.tv_nsec / 1000 / 1000);
#endif
}

gatekeeper::failure_record_t DefaultRecord(
    gatekeeper::secure_id_t secure_user_id) {
  return (gatekeeper::failure_record_t) {
    .secure_user_id = secure_user_id,
    .last_checked_timestamp = 0,
    .failure_counter = 0,
  };
}

static Result<secure_env::ManagedStorageData> RecordToStorageData(
    const gatekeeper::failure_record_t& record) {
  return CF_EXPECT(secure_env::CreateStorageData(&record, sizeof(record)));
}

static Result<gatekeeper::failure_record_t> StorageDataToRecord(
    const secure_env::StorageData& data) {
  gatekeeper::failure_record_t ret;
  CF_EXPECT(data.size == sizeof(ret), "StorageData buffer had an incorrect size.");
  memcpy(&ret, data.payload, data.size);
  return ret;
}

static Result<void> GetFailureRecordImpl(
    secure_env::Storage& storage,
    uint32_t uid,
    gatekeeper::secure_id_t secure_user_id,
    gatekeeper::failure_record_t *record) {
  std::string key = std::to_string(uid);
  if (!CF_EXPECT(storage.HasKey(key))) {
    auto data = CF_EXPECT(RecordToStorageData(DefaultRecord(secure_user_id)));
    CF_EXPECT(storage.Write(key, *data));
  }
  auto record_read = CF_EXPECT(storage.Read(key));
  auto record_decoded = CF_EXPECT(StorageDataToRecord(*record_read));
  if (record_decoded.secure_user_id == secure_user_id) {
    *record = record_decoded;
    return {};
  }
  LOG(DEBUG) << "User id mismatch for " << uid;
  auto record_to_write = DefaultRecord(secure_user_id);
  auto data = CF_EXPECT(RecordToStorageData(record_to_write));
  CF_EXPECT(storage.Write(key, *data));
  *record = record_to_write;
  return {};
}

bool TpmGatekeeper::GetFailureRecord(
    uint32_t uid,
    gatekeeper::secure_id_t secure_user_id,
    gatekeeper::failure_record_t *record,
    bool secure) {
  secure_env::Storage& storage = secure ? secure_storage_ : insecure_storage_;
  auto result = GetFailureRecordImpl(storage, uid, secure_user_id, record);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to get failure record: " << result.error().Message();
  }
  return result.ok();
}

static Result<void> WriteFailureRecordImpl(
    secure_env::Storage& storage,
    uint32_t uid,
    gatekeeper::failure_record_t* record) {
  std::string key = std::to_string(uid);
  auto data = CF_EXPECT(RecordToStorageData(*record));
  CF_EXPECT(storage.Write(key, *data));
  return {};
}

bool TpmGatekeeper::ClearFailureRecord(
    uint32_t uid, gatekeeper::secure_id_t secure_user_id, bool secure) {
  secure_env::Storage& storage = secure ? secure_storage_ : insecure_storage_;
  gatekeeper::failure_record_t record = DefaultRecord(secure_user_id);
  auto result = WriteFailureRecordImpl(storage, uid, &record);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to clear failure record: " << result.error().Message();
  }
  return result.ok();
}

bool TpmGatekeeper::WriteFailureRecord(
    uint32_t uid, gatekeeper::failure_record_t *record, bool secure) {
  secure_env::Storage& storage = secure ? secure_storage_ : insecure_storage_;
  auto result = WriteFailureRecordImpl(storage, uid, record);
  if (!result.ok()) {
    LOG(ERROR) << "Failed to write failure record: " << result.error().Message();
  }
  return result.ok();
}

bool TpmGatekeeper::IsHardwareBacked() const {
  return true;
}

}  // namespace cuttlefish

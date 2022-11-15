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

#include "host/commands/secure_env/tpm_keymaster_enforcement.h"

#include <android-base/endian.h>
#include <android-base/logging.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_hmac.h"
#include "host/commands/secure_env/tpm_key_blob_maker.h"
#include "host/commands/secure_env/tpm_random_source.h"

namespace cuttlefish {

using keymaster::HmacSharingParameters;
using keymaster::HmacSharingParametersArray;
using keymaster::KeymasterBlob;
using keymaster::KeymasterEnforcement;
using keymaster::km_id_t;
using keymaster::VerifyAuthorizationRequest;
using keymaster::VerifyAuthorizationResponse;

namespace {
inline bool operator==(const keymaster_blob_t& a, const keymaster_blob_t& b) {
  if (!a.data_length && !b.data_length) return true;
  if (!(a.data && b.data)) return a.data == b.data;
  return (a.data_length == b.data_length &&
          !memcmp(a.data, b.data, a.data_length));
}

bool operator==(const HmacSharingParameters& a,
                const HmacSharingParameters& b) {
  return a.seed == b.seed && !memcmp(a.nonce, b.nonce, sizeof(a.nonce));
}
}  // namespace
class CompareHmacSharingParams {
 public:
  bool operator()(const HmacSharingParameters& a,
                  const HmacSharingParameters& b) const {
    if (a.seed.data_length != b.seed.data_length) {
      return a.seed.data_length < b.seed.data_length;
    }
    auto res = memcmp(a.seed.data, b.seed.data, a.seed.data_length);
    if (res != 0) {
      return res < 0;
    }
    static_assert(sizeof(a.nonce) == sizeof(b.nonce));
    return memcmp(a.nonce, b.nonce, sizeof(a.nonce)) < 0;
  }
};

namespace {

uint64_t timespec_to_ms(const struct timespec& tp) {
  if (tp.tv_sec < 0) {
    return 0;
  }
  return static_cast<uint64_t>(tp.tv_sec) * 1000 +
         static_cast<uint64_t>(tp.tv_nsec) / 1000000;
}

uint64_t get_wall_clock_time_ms() {
  struct timespec tp;
  int err = clock_gettime(CLOCK_REALTIME, &tp);
  if (err) {
    return 0;
  }
  return timespec_to_ms(tp);
}

}  // namespace

TpmKeymasterEnforcement::TpmKeymasterEnforcement(
    TpmResourceManager& resource_manager, TpmGatekeeper& gatekeeper)
    : KeymasterEnforcement(64, 64),
      resource_manager_(resource_manager),
      gatekeeper_(gatekeeper) {}

TpmKeymasterEnforcement::~TpmKeymasterEnforcement() {}

bool TpmKeymasterEnforcement::activation_date_valid(
    uint64_t activation_date) const {
  return activation_date < get_wall_clock_time_ms();
}

bool TpmKeymasterEnforcement::expiration_date_passed(
    uint64_t expiration_date) const {
  return expiration_date < get_wall_clock_time_ms();
}

bool TpmKeymasterEnforcement::auth_token_timed_out(const hw_auth_token_t& token,
                                                   uint32_t timeout) const {
  // timeout comes in seconds, token.timestamp comes in milliseconds
  uint64_t timeout_ms = 1000 * (uint64_t)timeout;
  return (be64toh(token.timestamp) + timeout_ms) < get_current_time_ms();
}

uint64_t TpmKeymasterEnforcement::get_current_time_ms() const {
  struct timespec tp;
  int err = clock_gettime(CLOCK_BOOTTIME, &tp);
  if (err) {
    return 0;
  }
  return timespec_to_ms(tp);
}

keymaster_security_level_t TpmKeymasterEnforcement::SecurityLevel() const {
  return KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT;
}

bool TpmKeymasterEnforcement::ValidateTokenSignature(
    const hw_auth_token_t& token) const {
  hw_auth_token_t comparison_token = token;
  memset(comparison_token.hmac, 0, sizeof(comparison_token.hmac));

  /*
   * Should match implementation in system/gatekeeper/gatekeeper.cpp
   * GateKeeper::MintAuthToken
   */

  const uint8_t* auth_token_key = nullptr;
  uint32_t auth_token_key_len = 0;
  if (!gatekeeper_.GetAuthTokenKey(&auth_token_key, &auth_token_key_len)) {
    LOG(WARNING) << "Unable to get gatekeeper auth token";
    return false;
  }

  constexpr uint32_t hashable_length =
      sizeof(token.version) + sizeof(token.challenge) + sizeof(token.user_id) +
      sizeof(token.authenticator_id) + sizeof(token.authenticator_type) +
      sizeof(token.timestamp);

  static_assert(offsetof(hw_auth_token_t, hmac) == hashable_length,
                "hw_auth_token_t does not appear to be packed");

  gatekeeper_.ComputeSignature(
      comparison_token.hmac, sizeof(comparison_token.hmac), auth_token_key,
      auth_token_key_len, reinterpret_cast<uint8_t*>(&comparison_token),
      hashable_length);

  static_assert(sizeof(token.hmac) == sizeof(comparison_token.hmac));

  return memcmp(token.hmac, comparison_token.hmac, sizeof(token.hmac)) == 0;
}

keymaster_error_t TpmKeymasterEnforcement::GetHmacSharingParameters(
    HmacSharingParameters* params) {
  if (!have_saved_params_) {
    saved_params_.seed = {};
    TpmRandomSource random_source{resource_manager_.Esys()};
    auto rc = random_source.GenerateRandom(saved_params_.nonce,
                                           sizeof(saved_params_.nonce));
    if (rc != KM_ERROR_OK) {
      LOG(ERROR) << "Failed to generate HmacSharingParameters nonce";
      return rc;
    }
    have_saved_params_ = true;
  }
  params->seed = saved_params_.seed;
  memcpy(params->nonce, saved_params_.nonce, sizeof(params->nonce));
  return KM_ERROR_OK;
}

keymaster_error_t TpmKeymasterEnforcement::ComputeSharedHmac(
    const HmacSharingParametersArray& hmac_array, KeymasterBlob* sharingCheck) {
  std::set<HmacSharingParameters, CompareHmacSharingParams> sorted_hmac_inputs;
  bool found_mine = false;
  for (int i = 0; i < hmac_array.num_params; i++) {
    HmacSharingParameters sharing_params;
    sharing_params.seed =
        keymaster::KeymasterBlob(hmac_array.params_array[i].seed);
    memcpy(sharing_params.nonce, hmac_array.params_array[i].nonce,
           sizeof(sharing_params.nonce));
    found_mine = found_mine || (sharing_params == saved_params_);
    sorted_hmac_inputs.emplace(std::move(sharing_params));
  }

  if (!found_mine) return KM_ERROR_INVALID_ARGUMENT;

  // unique data has a low maximum size, so combine the hmac parameters
  char unique_data[] = "\0\0\0\0\0\0\0\0\0\0";
  int unique_index = 0;
  for (const auto& hmac_sharing : sorted_hmac_inputs) {
    for (size_t j = 0; j < hmac_sharing.seed.data_length; j++) {
      unique_data[unique_index % sizeof(unique_data)] ^=
          hmac_sharing.seed.data[j];
      unique_index++;
    }
    for (auto nonce_byte : hmac_sharing.nonce) {
      unique_data[unique_index % sizeof(unique_data)] ^= nonce_byte;
      unique_index++;
    }
  }

  static const uint8_t signing_input[] = "Keymaster HMAC Verification";
  auto hmac = TpmHmacWithContext(resource_manager_,
                                 std::string(unique_data, sizeof(unique_data)),
                                 signing_input, sizeof(signing_input));
  if (!hmac) {
    LOG(ERROR) << "Unable to complete signing check";
    return KM_ERROR_UNKNOWN_ERROR;
  }
  *sharingCheck = KeymasterBlob(hmac->buffer, hmac->size);

  return KM_ERROR_OK;
}

VerifyAuthorizationResponse TpmKeymasterEnforcement::VerifyAuthorization(
    const VerifyAuthorizationRequest& request) {
  struct VerificationData {
    uint64_t challenge;
    uint64_t timestamp;
    keymaster_security_level_t security_level;
  };
  VerifyAuthorizationResponse response(keymaster::kDefaultMessageVersion);
  response.error = KM_ERROR_UNKNOWN_ERROR;
  response.token.challenge = request.challenge;
  response.token.timestamp = get_current_time_ms();
  response.token.security_level = SecurityLevel();

  VerificationData verify_data{
      .challenge = response.token.challenge,
      .timestamp = response.token.timestamp,
      .security_level = response.token.security_level,
  };

  auto hmac = TpmHmacWithContext(resource_manager_, "verify_authorization",
                                 reinterpret_cast<uint8_t*>(&verify_data),
                                 sizeof(verify_data));
  if (!hmac) {
    LOG(ERROR) << "Could not calculate verification hmac";
    return response;
  } else if (hmac->size == 0) {
    LOG(ERROR) << "hmac was too short";
    return response;
  }
  response.token.mac = KeymasterBlob(hmac->buffer, hmac->size);
  response.error = KM_ERROR_OK;

  return response;
}

keymaster_error_t TpmKeymasterEnforcement::GenerateTimestampToken(
    keymaster::TimestampToken* token) {
  token->timestamp = get_current_time_ms();
  token->security_level = SecurityLevel();
  token->mac = KeymasterBlob();
  std::vector<uint8_t> token_buf_to_sign(token->SerializedSize(), 0);
  token->Serialize(token_buf_to_sign.data(),
                   token_buf_to_sign.data() + token_buf_to_sign.size());

  auto hmac =
      TpmHmacWithContext(resource_manager_, "timestamp_token",
                         token_buf_to_sign.data(), token_buf_to_sign.size());

  if (!hmac) {
    LOG(ERROR) << "Could not calculate timestamp token hmac";
    return KM_ERROR_UNKNOWN_ERROR;
  } else if (hmac->size == 0) {
    LOG(ERROR) << "hmac was too short";
    return KM_ERROR_UNKNOWN_ERROR;
  }
  token->mac = KeymasterBlob(hmac->buffer, hmac->size);

  return KM_ERROR_OK;
}

keymaster::KmErrorOr<std::array<uint8_t, 32>>
TpmKeymasterEnforcement::ComputeHmac(
    const std::vector<uint8_t>& data_to_mac) const {
  std::array<uint8_t, 32> result;

  const uint8_t* auth_token_key = nullptr;
  uint32_t auth_token_key_len = 0;
  if (!gatekeeper_.GetAuthTokenKey(&auth_token_key, &auth_token_key_len)) {
    LOG(WARNING) << "Unable to get gatekeeper auth token";
    return KM_ERROR_UNKNOWN_ERROR;
  }

  gatekeeper_.ComputeSignature(result.data(), result.size(), auth_token_key,
                               auth_token_key_len, data_to_mac.data(),
                               data_to_mac.size());
  return result;
}

bool TpmKeymasterEnforcement::CreateKeyId(const keymaster_key_blob_t& key_blob,
                                          km_id_t* keyid) const {
  auto hmac =
      TpmHmacWithContext(resource_manager_, "key_id", key_blob.key_material,
                         key_blob.key_material_size);
  if (!hmac) {
    LOG(ERROR) << "Failed to make a signature for a key id";
    return false;
  }
  if (hmac->size < sizeof(km_id_t)) {
    LOG(ERROR) << "hmac return size was less than " << sizeof(km_id_t)
               << ", got " << hmac->size;
    return false;
  }
  memcpy(keyid, hmac->buffer, sizeof(km_id_t));
  return true;
}

}  // namespace cuttlefish

/*
 * Copyright 2021, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/libs/confui/sign.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "host/libs/confui/sign_utils.h"

namespace cuttlefish {
namespace confui {
class HMacImplementation {
 public:
  static std::optional<support::hmac_t> hmac256(
      const support::auth_token_key_t& key,
      std::initializer_list<support::ByteBufferProxy> buffers);
};

std::optional<support::hmac_t> HMacImplementation::hmac256(
    const support::auth_token_key_t& key,
    std::initializer_list<support::ByteBufferProxy> buffers) {
  HMAC_CTX hmacCtx;
  HMAC_CTX_init(&hmacCtx);
  if (!HMAC_Init_ex(&hmacCtx, key.data(), key.size(), EVP_sha256(), nullptr)) {
    return {};
  }
  for (auto& buffer : buffers) {
    if (!HMAC_Update(&hmacCtx, buffer.data(), buffer.size())) {
      return {};
    }
  }
  support::hmac_t result;
  if (!HMAC_Final(&hmacCtx, result.data(), nullptr)) {
    return {};
  }
  return result;
}

/**
 * The test key is 32byte word with all bytes set to TestKeyBits::BYTE.
 */
enum class TestKeyBits : uint8_t {
  BYTE = 165 /* 0xA5 */,
};

std::optional<std::vector<std::uint8_t>> test_sign(
    const std::vector<std::uint8_t>& message) {
  // the same as userConfirm()
  using namespace support;
  auth_token_key_t key;
  key.fill(static_cast<std::uint8_t>(TestKeyBits::BYTE));
  using HMacer = HMacImplementation;
  auto confirm_signed_opt =
      HMacer::hmac256(key, {"confirmation token", message});
  if (!confirm_signed_opt) {
    return std::nullopt;
  }
  auto confirm_signed = confirm_signed_opt.value();
  return {
      std::vector<std::uint8_t>(confirm_signed.begin(), confirm_signed.end())};
}

std::optional<std::vector<std::uint8_t>> sign(
    const std::vector<std::uint8_t>& message) {
  return test_sign(message);
}
}  // namespace confui
}  // end of namespace cuttlefish

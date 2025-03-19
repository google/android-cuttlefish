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

#include <string>

#include <android-base/logging.h>

#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/confui_sign.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/confui/sign_utils.h"

namespace cuttlefish {
namespace confui {
namespace {
std::string GetSecureEnvSocketPath() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  CHECK(config) << "Config must not be null";
  auto instance = config->ForDefaultInstance();
  return instance.PerInstanceInternalUdsPath("confui_sign.sock");
}

/**
 * the secure_env signing server may be on slightly later than
 * confirmation UI host/webRTC process.
 */
SharedFD ConnectToSecureEnv() {
  auto socket_path = GetSecureEnvSocketPath();
  SharedFD socket_to_secure_env =
      SharedFD::SocketLocalClient(socket_path, false, SOCK_STREAM);
  return socket_to_secure_env;
}
}  // end of namespace

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

std::optional<std::vector<std::uint8_t>> TestSign(
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

std::optional<std::vector<std::uint8_t>> Sign(
    const std::vector<std::uint8_t>& message) {
  SharedFD socket_to_secure_env = ConnectToSecureEnv();
  if (!socket_to_secure_env->IsOpen()) {
    ConfUiLog(ERROR) << "Failed to connect to secure_env signing server.";
    return std::nullopt;
  }
  ConfUiSignRequester sign_client(socket_to_secure_env);
  // request signature
  sign_client.Request(message);
  auto response_opt = sign_client.Receive();
  if (!response_opt) {
    ConfUiLog(ERROR) << "Received nullopt";
    return std::nullopt;
  }
  // respond should be either error code or the signature
  auto response = std::move(response_opt.value());
  if (response.error_ != SignMessageError::kOk) {
    ConfUiLog(ERROR) << "Response was received with non-OK error code";
    return std::nullopt;
  }
  return {response.payload_};
}
}  // namespace confui
}  // end of namespace cuttlefish

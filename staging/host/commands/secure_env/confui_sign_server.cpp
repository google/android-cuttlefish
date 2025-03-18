//
// Copyright (C) 2022 The Android Open Source Project
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

#include "confui_sign_server.h"

#include <android-base/logging.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/tpm_hmac.h"
#include "host/libs/config/cuttlefish_config.h"
#include "tpm_keymaster_context.h"

namespace {

// Defined in
// hardware/interfaces/confirmationui/1.0/IConfirmationResultCallback.hal
constexpr const char kConfirmationTokenMessageTag[] = "confirmation token";

}  // namespace

namespace cuttlefish {
ConfUiSignServer::ConfUiSignServer(TpmResourceManager& tpm_resource_manager,
                                   SharedFD server_fd)
    : tpm_resource_manager_(tpm_resource_manager), server_fd_(server_fd) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  CHECK(config) << "Config must not be null";
  auto instance = config->ForDefaultInstance();
  server_socket_path_ = instance.PerInstanceInternalUdsPath("confui_sign.sock");
}

[[noreturn]] void ConfUiSignServer::MainLoop() {
  while (true) {
    if (!server_fd_->IsOpen()) {
      server_fd_ = SharedFD::SocketLocalServer(server_socket_path_, false,
                                               SOCK_STREAM, 0600);
    }
    auto accepted_socket_fd = SharedFD::Accept(*server_fd_);
    if (!accepted_socket_fd->IsOpen()) {
      LOG(ERROR) << "Confirmation UI host signing client socket is broken.";
      continue;
    }
    ConfUiSignSender sign_sender(accepted_socket_fd);

    // receive request
    auto request_opt = sign_sender.Receive();
    if (!request_opt) {
      std::string error_category = (sign_sender.IsIoError() ? "IO" : "Logic");
      LOG(ERROR) << "ReceiveRequest failed with " << error_category << " error";
      continue;
    }
    auto request = request_opt.value();

    // hmac over (prefix || data)
    std::vector<std::uint8_t> data{std::begin(kConfirmationTokenMessageTag),
                                   std::end(kConfirmationTokenMessageTag) - 1};

    data.insert(data.end(), request.payload_.data(),
                request.payload_.data() + request.payload_.size());
    auto hmac = TpmHmacWithContext(tpm_resource_manager_, "confirmation_token",
                                   data.data(), data.size());
    if (!hmac) {
      LOG(ERROR) << "Could not calculate confirmation token hmac";
      sign_sender.Send(confui::SignMessageError::kUnknownError, {});
      continue;
    }
    CHECK(hmac->size == keymaster::kConfirmationTokenSize)
        << "Hmac size for confirmation UI must be "
        << keymaster::kConfirmationTokenSize;

    // send hmac
    std::vector<std::uint8_t> hmac_buffer(hmac->buffer,
                                          hmac->buffer + hmac->size);
    if (!sign_sender.Send(confui::SignMessageError::kOk, hmac_buffer)) {
      LOG(ERROR) << "Sending signature failed likely due to I/O error";
    }
  }
}
}  // namespace cuttlefish

/*
 * Copyright 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/libs/security/confui_sign.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {
bool ConfUiSignerImpl::Send(SharedFD output,
                            const confui::SignMessageError error,
                            const std::vector<std::uint8_t>& payload) {
#define SET_FLAG_AND_RETURN_SEND \
  sign_status_ |= kIoError;      \
  return false

  // this looks redudant but makes sure that the byte-length of
  // the error is guaranteed to be the same when it is received
  confui::SignRawMessage msg;
  msg.error_ = error;
  auto n_written_err = WriteAllBinary(output, &(msg.error_));
  if (n_written_err != sizeof(msg.error_)) {
    sign_status_ |= kIoError;
    SET_FLAG_AND_RETURN_SEND;
  }
  decltype(msg.payload_.size()) payload_size = payload.size();
  auto n_written_payload_size = WriteAllBinary(output, &payload_size);
  if (n_written_payload_size != sizeof(payload_size)) {
    SET_FLAG_AND_RETURN_SEND;
  }
  const char* buf = reinterpret_cast<const char*>(payload.data());
  auto n_written_payload = WriteAll(output, buf, payload.size());

  if (n_written_payload != payload.size()) {
    SET_FLAG_AND_RETURN_SEND;
  }
  return true;
}

std::optional<confui::SignRawMessage> ConfUiSignerImpl::Receive(
    SharedFD input) {
  confui::SignRawMessage msg;

  auto n_read = ReadExactBinary(input, &(msg.error_));
  if (n_read != sizeof(msg.error_)) {
    sign_status_ |= kIoError;
  }
  if (msg.error_ != confui::SignMessageError::kOk) {
    sign_status_ |= kLogicError;
  }
  if (!IsOk()) {
    return std::nullopt;
  }

  decltype(msg.payload_.size()) payload_size = 0;
  n_read = ReadExactBinary(input, &payload_size);
  if (n_read != sizeof(payload_size)) {
    sign_status_ |= kIoError;
    return std::nullopt;
  }

  std::vector<std::uint8_t> buffer(payload_size);
  char* buf_data = reinterpret_cast<char*>(buffer.data());
  n_read = ReadExact(input, buf_data, payload_size);
  if (n_read != payload_size) {
    sign_status_ |= kIoError;
    return std::nullopt;
  }
  msg.payload_.swap(buffer);
  return {msg};
}

std::optional<confui::SignRawMessage> ConfUiSignSender::Receive() {
  return impl_.Receive(server_fd_);
}

bool ConfUiSignSender::Send(const SignMessageError error,
                            const std::vector<std::uint8_t>& encoded_hmac) {
  if (!impl_.IsOk()) {
    return false;
  }
  impl_.Send(server_fd_, error, encoded_hmac);
  return impl_.IsOk();
}

bool ConfUiSignRequester::Request(const std::vector<std::uint8_t>& message) {
  impl_.Send(client_fd_, confui::SignMessageError::kOk, message);
  return impl_.IsOk();
}

std::optional<confui::SignRawMessage> ConfUiSignRequester::Receive() {
  if (!impl_.IsOk()) {
    return std::nullopt;
  }
  return impl_.Receive(client_fd_);
}
}  // namespace cuttlefish

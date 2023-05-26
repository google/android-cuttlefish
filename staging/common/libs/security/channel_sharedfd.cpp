/*
 * Copyright 2023 The Android Open Source Project
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

#include "common/libs/security/channel_sharedfd.h"

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {
namespace secure_env {
namespace {

/**
 * Allocates memory for a RawMessage carrying a message of size
 * `payload_size`.
 */
Result<ManagedMessage> CreateMessage(uint32_t command, bool is_response, size_t payload_size) {
  const auto bytes_to_allocate = sizeof(RawMessage) + payload_size;
  auto memory = std::malloc(bytes_to_allocate);
  CF_EXPECT(memory != nullptr,
            "Cannot allocate " << bytes_to_allocate << " bytes for secure_env RPC message");
  auto message = reinterpret_cast<RawMessage*>(memory);
  message->command = command;
  message->is_response = is_response;
  message->payload_size = payload_size;
  return ManagedMessage(message);
}

}

SharedFdChannel::SharedFdChannel(SharedFD input, SharedFD output)
    : input_(std::move(input)), output_(std::move(output)) {}

Result<void> SharedFdChannel::SendRequest(uint32_t command, void* message, size_t message_size) {
  return SendMessage(command, false, message, message_size);
}

Result<void> SharedFdChannel::SendResponse(uint32_t command, void* message, size_t message_size) {
  return SendMessage(command, true, message, message_size);
}

Result<ManagedMessage> SharedFdChannel::ReceiveMessage() {
  struct RawMessage message_header;
  auto read = ReadExactBinary(input_, &message_header);
  CF_EXPECT(read == sizeof(RawMessage),
            "Expected " << sizeof(RawMessage) << ", received " << read << "\n" <<
            "Could not read message: " << input_->StrError());
  LOG(DEBUG) << "Received message with id: " << message_header.command;

  auto message = CF_EXPECT(CreateMessage(message_header.command, message_header.is_response,
                                         message_header.payload_size));
  auto message_bytes = reinterpret_cast<char*>(message->payload);
  read = ReadExact(input_, message_bytes, message->payload_size);
  CF_EXPECT(read == message->payload_size,
            "Could not read message: " << input_->StrError());

  return message;
}

Result<void> SharedFdChannel::SendMessage(uint32_t command, bool response,
                                          void* message, size_t message_size) {
  auto to_send = CF_EXPECT(CreateMessage(command, response, message_size));
  memcpy(to_send->payload, message, message_size);
  auto write_size = sizeof(RawMessage) + message_size;
  auto to_send_bytes = reinterpret_cast<const char*>(to_send.get());
  auto written = WriteAll(output_, to_send_bytes, write_size);
  CF_EXPECT(written != -1,
            "Could not write message: " << output_->StrError());
  return {};
}

}  // namespace secure_env
}  // namespace cuttlefish
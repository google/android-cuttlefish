/*
 * Copyright 2020 The Android Open Source Project
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

#include "common/libs/security/gatekeeper_channel_sharedfd.h"

#include <cstdlib>

#include <android-base/logging.h>
#include "keymaster/android_keymaster_utils.h"

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {
using gatekeeper::GatekeeperRawMessage;

SharedFdGatekeeperChannel::SharedFdGatekeeperChannel(SharedFD input,
                                                     SharedFD output)
    : input_(input), output_(output) {}

bool SharedFdGatekeeperChannel::SendRequest(
    uint32_t command, const gatekeeper::GateKeeperMessage& message) {
  return SendMessage(command, false, message);
}

bool SharedFdGatekeeperChannel::SendResponse(
    uint32_t command, const gatekeeper::GateKeeperMessage& message) {
  return SendMessage(command, true, message);
}

bool SharedFdGatekeeperChannel::SendMessage(
    uint32_t command, bool is_response,
    const gatekeeper::GateKeeperMessage& message) {
  LOG(DEBUG) << "Sending message with id: " << command;
  auto payload_size = message.GetSerializedSize();
  auto to_send = CreateGatekeeperMessage(command, is_response, payload_size);
  message.Serialize(to_send->payload, to_send->payload + payload_size);
  auto write_size = payload_size + sizeof(GatekeeperRawMessage);
  auto to_send_bytes = reinterpret_cast<const char*>(to_send.get());
  auto written = WriteAll(output_, to_send_bytes, write_size);
  if (written == -1) {
    LOG(ERROR) << "Could not write Gatekeeper Message: " << output_->StrError();
  }
  return written == write_size;
}

ManagedGatekeeperMessage SharedFdGatekeeperChannel::ReceiveMessage() {
  struct GatekeeperRawMessage message_header;
  auto read = ReadExactBinary(input_, &message_header);
  if (read != sizeof(GatekeeperRawMessage)) {
    LOG(ERROR) << "Expected " << sizeof(GatekeeperRawMessage) << ", received "
               << read;
    LOG(ERROR) << "Could not read Gatekeeper Message: " << input_->StrError();
    return {};
  }
  LOG(DEBUG) << "Received message with id: " << message_header.cmd;
  auto message =
      CreateGatekeeperMessage(message_header.cmd, message_header.is_response,
                              message_header.payload_size);
  auto message_bytes = reinterpret_cast<char*>(message->payload);
  read = ReadExact(input_, message_bytes, message->payload_size);
  if (read != message->payload_size) {
    LOG(ERROR) << "Could not read Gatekeeper Message: " << input_->StrError();
    return {};
  }
  return message;
}

}  // namespace cuttlefish
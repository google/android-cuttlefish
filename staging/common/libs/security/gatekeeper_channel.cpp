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

#include "common/libs/security/gatekeeper_channel.h"

#include <cstdlib>

#include <android-base/logging.h>
#include "keymaster/android_keymaster_utils.h"

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {

ManagedGatekeeperMessage CreateGatekeeperMessage(
    uint32_t command, bool is_response, size_t payload_size) {
  auto memory = std::malloc(payload_size + sizeof(GatekeeperRawMessage));
  auto message = reinterpret_cast<GatekeeperRawMessage*>(memory);
  message->cmd = command;
  message->is_response = is_response;
  message->payload_size = payload_size;
  return ManagedGatekeeperMessage(message);
}

void GatekeeperCommandDestroyer::operator()(GatekeeperRawMessage* ptr) {
  {
    keymaster::Eraser(ptr, sizeof(GatekeeperRawMessage) + ptr->payload_size);
  }
  std::free(ptr);
}

GatekeeperChannel::GatekeeperChannel(SharedFD channel) : channel_(channel) {
}

bool GatekeeperChannel::SendRequest(
    uint32_t command, const gatekeeper::GateKeeperMessage& message) {
  return SendMessage(command, false, message);
}

bool GatekeeperChannel::SendResponse(
    uint32_t command, const gatekeeper::GateKeeperMessage& message) {
  return SendMessage(command, true, message);
}

bool GatekeeperChannel::SendMessage(
    uint32_t command,
    bool is_response,
    const gatekeeper::GateKeeperMessage& message) {
  LOG(DEBUG) << "Sending message with id: " << command;
  auto payload_size = message.GetSerializedSize();
  auto to_send = CreateGatekeeperMessage(command, is_response, payload_size);
  message.Serialize(to_send->payload, to_send->payload + payload_size);
  auto write_size = payload_size + sizeof(GatekeeperRawMessage);
  auto to_send_bytes = reinterpret_cast<const char*>(to_send.get());
  auto written = WriteAll(channel_, to_send_bytes, write_size);
  if (written == -1) {
    LOG(ERROR) << "Could not write Gatekeeper Message: " << channel_->StrError();
  }
  return written == write_size;
}

ManagedGatekeeperMessage GatekeeperChannel::ReceiveMessage() {
  struct GatekeeperRawMessage message_header;
  auto read = ReadExactBinary(channel_, &message_header);
  if (read != sizeof(GatekeeperRawMessage)) {
    LOG(ERROR) << "Expected " << sizeof(GatekeeperRawMessage) << ", received "
               << read;
    LOG(ERROR) << "Could not read Gatekeeper Message: " << channel_->StrError();
    return {};
  }
  LOG(DEBUG) << "Received message with id: " << message_header.cmd;
  auto message = CreateGatekeeperMessage(message_header.cmd,
                                         message_header.is_response,
                                         message_header.payload_size);
  auto message_bytes = reinterpret_cast<char*>(message->payload);
  read = ReadExact(channel_, message_bytes, message->payload_size);
  if (read != message->payload_size) {
    LOG(ERROR) << "Could not read Gatekeeper Message: " << channel_->StrError();
    return {};
  }
  return message;
}

} // namespace cuttlefish

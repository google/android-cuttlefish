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

#include "keymaster_channel.h"

#include <android-base/logging.h>
#include "keymaster/android_keymaster_utils.h"

#include "common/libs/fs/shared_buf.h"

namespace cvd {

ManagedKeymasterMessage CreateKeymasterMessage(
    keymaster_command command, size_t payload_size) {
  auto memory = new uint8_t[payload_size + sizeof(keymaster_message)];
  auto message = reinterpret_cast<keymaster_message*>(memory);
  message->cmd = command;
  message->payload_size = payload_size;
  return ManagedKeymasterMessage(message);
}

void KeymasterCommandDestroyer::operator()(keymaster_message* ptr) {
  {
    keymaster::Eraser(ptr, sizeof(keymaster_message) + ptr->payload_size);
  }
  delete reinterpret_cast<uint8_t*>(ptr);
}

KeymasterChannel::KeymasterChannel(SharedFD channel) : channel_(channel) {
}

bool KeymasterChannel::SendMessage(
    keymaster_command command, const keymaster::Serializable& message) {
  LOG(DEBUG) << "Sending message with id: " << command;
  auto payload_size = message.SerializedSize();
  auto to_send = CreateKeymasterMessage(command, payload_size);
  message.Serialize(to_send->payload, to_send->payload + payload_size);
  auto write_size = payload_size + sizeof(keymaster_message);
  auto to_send_bytes = reinterpret_cast<const char*>(to_send.get());
  auto written = cvd::WriteAll(channel_, to_send_bytes, write_size);
  if (written == -1) {
    LOG(ERROR) << "Could not write Keymaster Message: " << channel_->StrError();
  }
  return written == write_size;
}

ManagedKeymasterMessage KeymasterChannel::ReceiveMessage() {
  struct keymaster_message message_header;
  auto read = cvd::ReadExactBinary(channel_, &message_header);
  if (read != sizeof(keymaster_message)) {
    LOG(ERROR) << "Expected " << sizeof(keymaster_message) << ", received "
               << read;
    LOG(ERROR) << "Could not read Keymaster Message: " << channel_->StrError();
    return {};
  }
  LOG(DEBUG) << "Received message with id: " << message_header.cmd;
  auto message =
      CreateKeymasterMessage(message_header.cmd, message_header.payload_size);
  auto message_bytes = reinterpret_cast<char*>(message->payload);
  read = cvd::ReadExact(channel_, message_bytes, message->payload_size);
  if (read != message->payload_size) {
    LOG(ERROR) << "Could not read Keymaster Message: " << channel_->StrError();
    return {};
  }
  return message;
}

}

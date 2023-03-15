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

#include "common/libs/security/keymaster_channel_sharedfd.h"

#include <cstdlib>
#include <memory>
#include <ostream>
#include <string>

#include <android-base/logging.h>
#include <keymaster/android_keymaster_messages.h>
#include <keymaster/mem.h>
#include <keymaster/serializable.h>

#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {

SharedFdKeymasterChannel::SharedFdKeymasterChannel(SharedFD input,
                                                   SharedFD output)
    : input_(input), output_(output) {}

bool SharedFdKeymasterChannel::SendRequest(
    AndroidKeymasterCommand command, const keymaster::Serializable& message) {
  return SendMessage(command, false, message);
}

bool SharedFdKeymasterChannel::SendResponse(
    AndroidKeymasterCommand command, const keymaster::Serializable& message) {
  return SendMessage(command, true, message);
}

bool SharedFdKeymasterChannel::SendMessage(
    AndroidKeymasterCommand command, bool is_response,
    const keymaster::Serializable& message) {
  auto payload_size = message.SerializedSize();
  LOG(VERBOSE) << "Sending message with id: " << command << " and size "
               << payload_size;
  auto to_send = CreateKeymasterMessage(command, is_response, payload_size);
  message.Serialize(to_send->payload, to_send->payload + payload_size);
  auto write_size = payload_size + sizeof(keymaster_message);
  auto to_send_bytes = reinterpret_cast<const char*>(to_send.get());
  auto written = WriteAll(output_, to_send_bytes, write_size);
  if (written != write_size) {
    LOG(ERROR) << "Could not write Keymaster Message: " << output_->StrError();
  }
  return written == write_size;
}

ManagedKeymasterMessage SharedFdKeymasterChannel::ReceiveMessage() {
  struct keymaster_message message_header;
  auto read = ReadExactBinary(input_, &message_header);
  if (read != sizeof(keymaster_message)) {
    LOG(ERROR) << "Expected " << sizeof(keymaster_message) << ", received "
               << read;
    LOG(ERROR) << "Could not read Keymaster Message: " << input_->StrError();
    return {};
  }
  LOG(VERBOSE) << "Received message with id: " << message_header.cmd
               << " and size " << message_header.payload_size;
  auto message =
      CreateKeymasterMessage(message_header.cmd, message_header.is_response,
                             message_header.payload_size);
  auto message_bytes = reinterpret_cast<char*>(message->payload);
  read = ReadExact(input_, message_bytes, message->payload_size);
  if (read != message->payload_size) {
    LOG(ERROR) << "Could not read Keymaster Message: " << input_->StrError();
    return {};
  }
  return message;
}

}  // namespace cuttlefish
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

#include "cuttlefish/common/libs/transport/channel_sharedfd.h"

#include <poll.h>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_buf.h"

namespace cuttlefish {
namespace transport {

SharedFdChannel::SharedFdChannel(SharedFD input, SharedFD output)
    : input_(std::move(input)), output_(std::move(output)) {}

Result<void> SharedFdChannel::SendRequest(RawMessage& message) {
  return SendMessage(message, false);
}

Result<void> SharedFdChannel::SendResponse(RawMessage& message) {
  return SendMessage(message, true);
}

Result<ManagedMessage> SharedFdChannel::ReceiveMessage() {
  struct RawMessage message_header;
  auto read = ReadExactBinary(input_, &message_header);
  CF_EXPECT(read == sizeof(RawMessage),
            "Expected " << sizeof(RawMessage) << ", received " << read << "\n"
                        << "Could not read message: " << input_->StrError());
  LOG(DEBUG) << "Received message with id: " << message_header.command;

  auto message = CF_EXPECT(CreateMessage(message_header.command,
                                         message_header.is_response,
                                         message_header.payload_size));
  auto message_bytes = reinterpret_cast<char*>(message->payload);
  read = ReadExact(input_, message_bytes, message->payload_size);
  CF_EXPECT(read == message->payload_size,
            "Could not read message: " << input_->StrError());

  return message;
}

Result<int> SharedFdChannel::WaitForMessage() {
  std::vector<PollSharedFd> input_poll = {
      {.fd = input_, .events = POLLIN},
  };
  const int poll_result = SharedFD::Poll(input_poll, -1);

  CF_EXPECT(poll_result >= 0,
            "Cannot execute poll on input stream to wait for incoming message");

  return poll_result;
}

Result<void> SharedFdChannel::SendMessage(RawMessage& message, bool response) {
  message.is_response = response;
  auto write_size = sizeof(RawMessage) + message.payload_size;
  auto message_bytes = reinterpret_cast<const char*>(&message);
  auto written = WriteAll(output_, message_bytes, write_size);
  CF_EXPECT(written == write_size,
            "Could not write message: " << output_->StrError());
  return {};
}

}  // namespace transport
}  // namespace cuttlefish
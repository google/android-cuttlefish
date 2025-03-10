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

#include "common/libs/transport/channel.h"

namespace cuttlefish {
namespace transport {

void MessageDestroyer::operator()(RawMessage* ptr) {
  std::memset(ptr, 0, sizeof(RawMessage) + ptr->payload_size);
  std::free(ptr);
}

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

Result<ManagedMessage> CreateMessage(uint32_t command, size_t payload_size) {
  return CreateMessage(command, false, payload_size);
}

}  // namespace transport
}  // namespace cuttlefish
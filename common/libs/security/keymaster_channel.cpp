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

#include "common/libs/security/keymaster_channel.h"

namespace cuttlefish {

void KeymasterCommandDestroyer::operator()(keymaster_message* ptr) {
  { keymaster::Eraser(ptr, sizeof(keymaster_message) + ptr->payload_size); }
  std::free(ptr);
}

ManagedKeymasterMessage CreateKeymasterMessage(AndroidKeymasterCommand command,
                                               bool is_response,
                                               size_t payload_size) {
  auto memory = std::malloc(payload_size + sizeof(keymaster_message));
  auto message = reinterpret_cast<keymaster_message*>(memory);
  message->cmd = command;
  message->is_response = is_response;
  message->payload_size = payload_size;
  return ManagedKeymasterMessage(message);
}

}  // namespace cuttlefish
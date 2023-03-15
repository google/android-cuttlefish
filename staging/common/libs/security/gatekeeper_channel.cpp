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

#include <keymaster/android_keymaster_utils.h>

namespace cuttlefish {

void GatekeeperCommandDestroyer::operator()(GatekeeperRawMessage* ptr) {
  {
    keymaster::Eraser(ptr, sizeof(GatekeeperRawMessage) + ptr->payload_size);
  }
  std::free(ptr);
}

ManagedGatekeeperMessage CreateGatekeeperMessage(uint32_t command,
                                                 bool is_response,
                                                 size_t payload_size) {
  auto memory = std::malloc(payload_size + sizeof(GatekeeperRawMessage));
  auto message = reinterpret_cast<GatekeeperRawMessage*>(memory);
  message->cmd = command;
  message->is_response = is_response;
  message->payload_size = payload_size;
  return ManagedGatekeeperMessage(message);
}

}  // namespace cuttlefish
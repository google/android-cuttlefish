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

#pragma once

#include "gatekeeper/gatekeeper_messages.h"

#include "common/libs/security/channel.h"

namespace cuttlefish {

/*
 * Interface for communication channels that synchronously communicate
 * Gatekeeper IPC/RPC calls.
 *
 * TODO(b/286027243): Remove this class and use Channel instead
 */
class GatekeeperChannel {
 public:
  virtual bool SendRequest(uint32_t command,
                           const gatekeeper::GateKeeperMessage& message) = 0;
  virtual bool SendResponse(uint32_t command,
                            const gatekeeper::GateKeeperMessage& message) = 0;
  virtual secure_env::ManagedMessage ReceiveMessage() = 0;
  virtual ~GatekeeperChannel() {}
};

}  // namespace cuttlefish
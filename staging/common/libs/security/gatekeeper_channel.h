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

#include "common/libs/fs/shared_fd.h"

#include <memory>

namespace gatekeeper {

/**
 * GatekeeperRawMessage - Header and raw byte payload for a serialized
 * gatekeeper message.
 *
 * @cmd: the command, one of gatekeeper::ENROLL and gatekeeper::VERIFY.
 * @payload: start of the serialized command specific payload
 */
struct GatekeeperRawMessage {
    uint32_t cmd : 31;
    bool is_response : 1;
    uint32_t payload_size;
    uint8_t payload[0];
};

} // namespace gatekeeper

namespace cuttlefish {

using gatekeeper::GatekeeperRawMessage;

/**
 * A destroyer for GatekeeperRawMessage instances created with
 * CreateGatekeeperMessage. Wipes memory from the GatekeeperRawMessage instances.
 */
class GatekeeperCommandDestroyer {
public:
  void operator()(GatekeeperRawMessage* ptr);
};

/** An owning pointer for a GatekeeperRawMessage instance. */
using ManagedGatekeeperMessage =
    std::unique_ptr<GatekeeperRawMessage, GatekeeperCommandDestroyer>;

/**
 * Allocates memory for a GatekeeperRawMessage carrying a message of size
 * `payload_size`.
 */
ManagedGatekeeperMessage CreateGatekeeperMessage(
    uint32_t command, bool is_response, size_t payload_size);

/*
 * Interface for communication channels that synchronously communicate Gatekeeper
 * IPC/RPC calls. Sends messages over a file descriptor.
 */
class GatekeeperChannel {
public:
  GatekeeperChannel(SharedFD channel);

  bool SendRequest(uint32_t command,
                   const gatekeeper::GateKeeperMessage& message);
  bool SendResponse(uint32_t command,
                    const gatekeeper::GateKeeperMessage& message);
  ManagedGatekeeperMessage ReceiveMessage();
private:
  SharedFD channel_;
  bool SendMessage(uint32_t command, bool response,
                   const gatekeeper::GateKeeperMessage& message);
};

} // namespace cuttlefish

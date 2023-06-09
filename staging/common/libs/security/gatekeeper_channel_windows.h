/*
 * Copyright 2022 The Android Open Source Project
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

#include <windows.h>
#undef ERROR_RETRY
#include <gatekeeper/gatekeeper_messages.h>

#include <string>

#include "common/libs/security/gatekeeper_channel.h"

namespace cuttlefish {

/*
 * Interface for communication channels that synchronously communicate
 * Gatekeeper IPC/RPC calls. Sends messages over a named pipe.
 */
class GatekeeperWindowsChannel : public GatekeeperChannel {
 public:
  ~GatekeeperWindowsChannel();

  static std::unique_ptr<GatekeeperWindowsChannel> Create(HANDLE pipe_handle);
  bool SendRequest(uint32_t command,
                   const gatekeeper::GateKeeperMessage& message) override;
  bool SendResponse(uint32_t command,
                    const gatekeeper::GateKeeperMessage& message) override;
  secure_env::ManagedMessage ReceiveMessage() override;

 protected:
  GatekeeperWindowsChannel() = default;

 private:
  bool WaitForConnection(HANDLE pipe_handle);
  bool SendMessage(uint32_t command, bool response,
                   const gatekeeper::GateKeeperMessage& message);
  bool ReadFromPipe(LPVOID buffer, DWORD size);

  // Handle to the (asynchronous) named pipe.
  HANDLE pipe_handle_ = NULL;
  // OVERLAPPED struct for the named pipe. It contains an event object and is
  // used to wait for asynchronous pipe operations.
  OVERLAPPED pipe_overlapped_ = {};
};

}  // namespace cuttlefish
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
#include <string>

#include <keymaster/android_keymaster_messages.h>
#include <keymaster/serializable.h>

#include "common/libs/security/keymaster_channel.h"

namespace cuttlefish {

using keymaster::AndroidKeymasterCommand;

/*
 * Interface for communication channels that synchronously communicate Keymaster
 * IPC/RPC calls. Sends messages over a named pipe.
 */
class KeymasterWindowsChannel : public KeymasterChannel {
 public:
  ~KeymasterWindowsChannel();

  static std::unique_ptr<KeymasterWindowsChannel> Create(HANDLE pipe_handle);

  bool SendRequest(AndroidKeymasterCommand command,
                   const keymaster::Serializable& message) override;
  bool SendResponse(AndroidKeymasterCommand command,
                    const keymaster::Serializable& message) override;
  ManagedKeymasterMessage ReceiveMessage() override;

 protected:
  KeymasterWindowsChannel() = default;

 private:
  bool WaitForConnection(HANDLE pipe_handle);

  bool SendMessage(AndroidKeymasterCommand command, bool response,
                   const keymaster::Serializable& message);

  bool ReadFromPipe(LPVOID buffer, DWORD size);

  // Handle to the (asynchronous) named pipe.
  HANDLE pipe_handle_ = NULL;
  // OVERLAPPED struct for the named pipe. It contains an event object and is
  // used to wait for asynchronous pipe operations.
  OVERLAPPED pipe_overlapped_ = {};
};

}  // namespace cuttlefish
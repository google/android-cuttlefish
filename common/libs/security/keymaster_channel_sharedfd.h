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

#include <keymaster/android_keymaster_messages.h>
#include <keymaster/serializable.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/keymaster_channel.h"

namespace cuttlefish {

/*
 * Interface for communication channels that synchronously communicate Keymaster
 * IPC/RPC calls. Sends messages over a file descriptor.
 */
class SharedFdKeymasterChannel : public KeymasterChannel {
 public:
  SharedFdKeymasterChannel(SharedFD input, SharedFD output);

  bool SendRequest(AndroidKeymasterCommand command,
                   const keymaster::Serializable& message) override;
  bool SendResponse(AndroidKeymasterCommand command,
                    const keymaster::Serializable& message) override;
  ManagedKeymasterMessage ReceiveMessage() override;

 private:
  SharedFD input_;
  SharedFD output_;
  bool SendMessage(keymaster::AndroidKeymasterCommand command, bool response,
                   const keymaster::Serializable& message);
};

}  // namespace cuttlefish
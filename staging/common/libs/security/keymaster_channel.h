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

#include "keymaster/android_keymaster_messages.h"
#include "keymaster/serializable.h"

#include "common/libs/fs/shared_fd.h"

#include <memory>

namespace keymaster {

/**
 * keymaster_message - Serial header for communicating with KM server
 * @cmd: the command, one of AndroidKeymasterCommand.
 * @payload: start of the serialized command specific payload
 */
struct keymaster_message {
    AndroidKeymasterCommand cmd : 31;
    bool is_response : 1;
    uint32_t payload_size;
    uint8_t payload[0];
};

} // namespace keymaster

namespace cuttlefish {

using keymaster::AndroidKeymasterCommand;
using keymaster::keymaster_message;

/**
 * A destroyer for keymaster_message instances created with
 * CreateKeymasterMessage. Wipes memory from the keymaster_message instances.
 */
class KeymasterCommandDestroyer {
public:
  void operator()(keymaster_message* ptr);
};

/** An owning pointer for a keymaster_message instance. */
using ManagedKeymasterMessage =
    std::unique_ptr<keymaster_message, KeymasterCommandDestroyer>;

/**
 * Allocates memory for a keymaster_message carrying a message of size
 * `payload_size`.
 */
ManagedKeymasterMessage CreateKeymasterMessage(
    AndroidKeymasterCommand command, bool is_response, size_t payload_size);

/*
 * Interface for communication channels that synchronously communicate Keymaster
 * IPC/RPC calls. Sends messages over a file descriptor.
 */
class KeymasterChannel {
private:
  SharedFD channel_;
  bool SendMessage(AndroidKeymasterCommand command, bool response,
                   const keymaster::Serializable& message);
public:
  KeymasterChannel(SharedFD channel);

  bool SendRequest(AndroidKeymasterCommand command,
                   const keymaster::Serializable& message);
  bool SendResponse(AndroidKeymasterCommand command,
                    const keymaster::Serializable& message);
  ManagedKeymasterMessage ReceiveMessage();
};

} // namespace cuttlefish

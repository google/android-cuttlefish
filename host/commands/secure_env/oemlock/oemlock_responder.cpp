//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/secure_env/oemlock/oemlock_responder.h"

#include "common/libs/security/oemlock.h"

namespace cuttlefish {
namespace oemlock {

OemLockResponder::OemLockResponder(secure_env::Channel& channel,
                                   OemLock& oemlock)
    : channel_(channel), oemlock_(oemlock) {}

Result<void> OemLockResponder::ProcessMessage() {
  auto request = CF_EXPECT(channel_.ReceiveMessage(), "Could not receive message");

  bool result = false;
  switch(secure_env::OemLockField(request->command)) {
    case secure_env::OemLockField::ALLOWED_BY_CARRIER: {
      if (request->payload_size == 0) {
        result = CF_EXPECT(oemlock_.IsOemUnlockAllowedByCarrier());
      } else if (request->payload_size == sizeof(bool)) {
        result = *reinterpret_cast<bool*>(request->payload);
        CF_EXPECT(oemlock_.SetOemUnlockAllowedByCarrier(result));
      }
      break;
    }

    case secure_env::OemLockField::ALLOWED_BY_DEVICE: {
      if (request->payload_size == 0) {
        result = CF_EXPECT(oemlock_.IsOemUnlockAllowedByDevice());
      } else if (request->payload_size == sizeof(bool)) {
        result = *reinterpret_cast<bool*>(request->payload);
        CF_EXPECT(oemlock_.SetOemUnlockAllowedByDevice(result));
      }
      break;
    }

    case secure_env::OemLockField::ALLOWED: {
      if (request->payload_size == 0) {
        result = CF_EXPECT(oemlock_.IsOemUnlockAllowed());
      }
      break;
    }

    case secure_env::OemLockField::LOCKED: {
      if (request->payload_size == 0) {
        result = CF_EXPECT(oemlock_.IsOemLocked());
      } else if (request->payload_size == sizeof(bool)) {
        result = *reinterpret_cast<bool*>(request->payload);
        CF_EXPECT(oemlock_.SetOemLocked(result));
      }
      break;
    }

    default:
      return CF_ERR("Unrecognized message id " << reinterpret_cast<uint32_t>(request->command));
  }

  auto message = CF_EXPECT(secure_env::CreateMessage(request->command, sizeof(bool)),
                           "Failed to allocate message for oemlock response");
  memcpy(message->payload, &result, sizeof(bool));
  CF_EXPECT(channel_.SendResponse(*message),
            "Could not answer to " << reinterpret_cast<uint32_t>(request->command) << " request");

  return {};
}

}  // namespace oemlock
}  // namespace cuttlefish

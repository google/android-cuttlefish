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

#include "host/commands/secure_env/oemlock_responder.h"

#include <android-base/logging.h>

#include "common/libs/security/oemlock.h"

namespace cuttlefish {
namespace oemlock {

OemLockResponder::OemLockResponder(secure_env::Channel& channel,
                                   OemLock& oemlock)
    : channel_(channel), oemlock_(oemlock) {}

Result<void> OemLockResponder::ProcessMessage() {
  auto request = CF_EXPECT(channel_.ReceiveMessage(), "Could not receive message");

  bool allowed = false;
  switch(secure_env::OemLockField(request->command)) {
    case secure_env::OemLockField::ALLOWED_BY_CARRIER: {
      if (request->payload_size == 0) {
        allowed = oemlock_.IsOemUnlockAllowedByCarrier();
      } else if (request->payload_size == sizeof(bool)) {
        allowed = *reinterpret_cast<bool*>(request->payload);
        oemlock_.SetOemUnlockAllowedByCarrier(allowed);
      }
      break;
    }

    case secure_env::OemLockField::ALLOWED_BY_DEVICE: {
      if (request->payload_size == 0) {
        allowed = oemlock_.IsOemUnlockAllowedByDevice();
      } else if (request->payload_size == sizeof(bool)) {
        allowed = *reinterpret_cast<bool*>(request->payload);
        oemlock_.SetOemUnlockAllowedByDevice(allowed);
      }
      break;
    }

    default:
      return CF_ERR("Unrecognized message id " << reinterpret_cast<uint32_t>(request->command));
  }

  CF_EXPECT(channel_.SendResponse(request->command, &allowed, sizeof(bool)),
            "Could not answer to " << reinterpret_cast<uint32_t>(request->command) << " request");

  return {};
}

}  // namespace oemlock
}  // namespace cuttlefish

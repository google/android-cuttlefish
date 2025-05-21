/*
 * Copyright 2024 The Android Open Source Project
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

#include "jcardsim_responder.h"

#include <android-base/logging.h>
#include <keymaster/android_keymaster_messages.h>

namespace cuttlefish {
constexpr const int kUnusedCommandField = 0;

JcardSimResponder::JcardSimResponder(SharedFdChannel& channel,
                                     const JCardSimInterface& jcs_interface)
    : channel_(channel), jcs_interface_(jcs_interface) {}

Result<ManagedMessage> JcardSimResponder::ToMessage(
    const std::vector<uint8_t>& data) {
  auto msg = CF_EXPECT(
      transport::CreateMessage(kUnusedCommandField, true, data.size()));
  std::copy(data.begin(), data.end(), msg->payload);
  return msg;
}

Result<void> JcardSimResponder::ProcessMessage() {
  auto request =
      CF_EXPECT(channel_.ReceiveMessage(), "Could not receive message");
  auto resp = CF_EXPECT(
      jcs_interface_.Transmit(request->payload, request->payload_size));
  auto msg = CF_EXPECT(ToMessage(resp), "Failed to convert to Message");
  return channel_.SendResponse(*msg);
}

}  // namespace cuttlefish

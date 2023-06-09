/*
 * Copyright 2023 The Android Open Source Project
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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/channel.h"

namespace cuttlefish {
namespace secure_env {

class SharedFdChannel : public Channel {
 public:
  SharedFdChannel(SharedFD input, SharedFD output);
  Result<void> SendRequest(RawMessage& message) override;
  Result<void> SendResponse(RawMessage& message) override;
  Result<ManagedMessage> ReceiveMessage() override;

 private:
  SharedFD input_;
  SharedFD output_;

  Result<void> SendMessage(RawMessage& message, bool response);
};

}  // namespace secure_env
}  // namespace cuttlefish
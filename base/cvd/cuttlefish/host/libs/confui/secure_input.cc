/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/libs/confui/secure_input.h"

namespace cuttlefish {
namespace confui {
namespace {

template <typename T>
auto CheckAndReturnSessionId(const std::unique_ptr<T>& msg) {
  CHECK(msg) << "ConfUiUserSelectionMessage must not be null";
  return msg->GetSessionId();
}

}  // end of namespace

ConfUiSecureUserSelectionMessage::ConfUiSecureUserSelectionMessage(
    std::unique_ptr<ConfUiUserSelectionMessage>&& msg, const bool secure)
    : ConfUiMessage(CheckAndReturnSessionId(msg)),
      msg_(std::move(msg)),
      is_secure_(secure) {}

ConfUiSecureUserTouchMessage::ConfUiSecureUserTouchMessage(
    std::unique_ptr<ConfUiUserTouchMessage>&& msg, const bool secure)
    : ConfUiMessage(CheckAndReturnSessionId(msg)),
      msg_(std::move(msg)),
      is_secure_(secure) {}

std::unique_ptr<ConfUiSecureUserSelectionMessage> ToSecureSelectionMessage(
    std::unique_ptr<ConfUiUserSelectionMessage>&& msg, const bool secure) {
  return std::make_unique<ConfUiSecureUserSelectionMessage>(std::move(msg),
                                                            secure);
}

std::unique_ptr<ConfUiSecureUserTouchMessage> ToSecureTouchMessage(
    std::unique_ptr<ConfUiUserTouchMessage>&& msg, const bool secure) {
  return std::make_unique<ConfUiSecureUserTouchMessage>(std::move(msg), secure);
}

}  // end of namespace confui
}  // end of namespace cuttlefish

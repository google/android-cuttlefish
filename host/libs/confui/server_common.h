/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <cstdint>
#include <memory>
#include <vector>

#include "common/libs/confui/confui.h"

namespace cuttlefish {
namespace confui {
enum class MainLoopState : std::uint32_t {
  kInit = 1,
  kInSession = 2,
  kWaitStop = 3,  // wait ack after sending confirm/cancel
  kAwaitCleanup = 5,
  kTerminated = 8,
  kInvalid = 9
};

using TeeUiFrame = std::vector<std::uint32_t>;

// FSM input to Session FSM
enum class FsmInput : std::uint32_t {
  kUserEvent = 1,
  kHalStart,
  kHalStop,
  kHalAbort,
  kHalUnknown
};

std::string ToString(FsmInput input);
std::string ToString(const MainLoopState& state);

FsmInput ToFsmInput(const ConfUiMessage& msg);

std::unique_ptr<ConfUiMessage> CreateFromUserSelection(
    const std::string& session_id, const UserResponse::type user_selection);
}  // end of namespace confui
}  // end of namespace cuttlefish

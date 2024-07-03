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

#include "host/libs/confui/server_common.h"
namespace cuttlefish {
namespace confui {
FsmInput ToFsmInput(const ConfUiMessage& msg) {
  const auto cmd = msg.GetType();
  switch (cmd) {
    case ConfUiCmd::kUserInputEvent:
      return FsmInput::kUserEvent;
    case ConfUiCmd::kUnknown:
      return FsmInput::kHalUnknown;
    case ConfUiCmd::kStart:
      return FsmInput::kHalStart;
    case ConfUiCmd::kStop:
      return FsmInput::kHalStop;
    case ConfUiCmd::kAbort:
      return FsmInput::kHalAbort;
    case ConfUiCmd::kCliAck:
    case ConfUiCmd::kCliRespond:
    default:
      ConfUiLog(FATAL) << "The" << ToString(cmd)
                       << "is not handled by the Session FSM but "
                       << "directly calls Abort()";
  }
  return FsmInput::kHalUnknown;
}

std::string ToString(FsmInput input) {
  switch (input) {
    case FsmInput::kUserEvent:
      return {"kUserEvent"};
    case FsmInput::kHalStart:
      return {"kHalStart"};
    case FsmInput::kHalStop:
      return {"kHalStop"};
    case FsmInput::kHalAbort:
      return {"kHalAbort"};
    case FsmInput::kHalUnknown:
    default:
      break;
  }
  return {"kHalUnknown"};
}

std::string ToString(const MainLoopState& state) {
  switch (state) {
    case MainLoopState::kInit:
      return "kInit";
    case MainLoopState::kInSession:
      return "kInSession";
    case MainLoopState::kWaitStop:
      return "kWaitStop";
    case MainLoopState::kAwaitCleanup:
      return "kAwaitCleanup";
    case MainLoopState::kTerminated:
      return "kTerminated";
    default:
      return "kInvalid";
  }
}

}  // end of namespace confui
}  // end of namespace cuttlefish

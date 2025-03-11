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
std::unique_ptr<ConfUiMessage> CreateFromUserSelection(
    const std::string& session_id, const UserResponse::type user_selection) {
  return std::make_unique<ConfUiUserSelectionMessage>(session_id,
                                                      user_selection);
}

static FsmInput UserEvtToFsmInput(UserResponse::type user_response) {
  if (user_response == UserResponse::kConfirm) {
    return FsmInput::kUserConfirm;
  }
  if (user_response == UserResponse::kCancel) {
    return FsmInput::kUserCancel;
  }
  if (user_response == UserResponse::kUserAbort) {
    return FsmInput::kUserAbort;
  }
  return FsmInput::kUserUnknown;
}

FsmInput ToFsmInput(const ConfUiMessage& msg) {
  const auto cmd = msg.GetType();
  if (cmd == ConfUiCmd::kUserInputEvent) {
    const auto& user_input =
        static_cast<const ConfUiUserSelectionMessage&>(msg);
    const auto& response = user_input.GetResponse();
    return UserEvtToFsmInput(response);
  }
  const auto hal_cmd = cmd;
  switch (hal_cmd) {
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
      ConfUiLog(FATAL) << "The" << ToString(hal_cmd)
                       << "is not handled by the Session FSM but "
                       << "directly calls Abort()";
  }
  return FsmInput::kHalUnknown;
}

std::string ToString(FsmInput input) {
  switch (input) {
    case FsmInput::kUserConfirm:
      return {"kUserConfirm"};
    case FsmInput::kUserCancel:
      return {"kUserCancel"};
    case FsmInput::kUserAbort:
      return {"kUserAbort"};
    case FsmInput::kUserUnknown:
      return {"kUserUnknown"};
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

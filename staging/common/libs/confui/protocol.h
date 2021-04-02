//
// Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#include <cstdint>
#include <string>

namespace cuttlefish {
namespace confui {
// When you update this, please update all the utility functions
// in conf.cpp: e.g. ToString, etc
enum class ConfUiCmd : std::uint32_t {
  kUnknown = 100,
  kStart = 111,   // start rendering, send confirmation msg, & wait respond
  kStop = 112,    // start rendering, send confirmation msg, & wait respond
  kCliAck = 113,  // client acknowledged. "error:err_msg" or "success:command"
  kCliRespond = 114,  //  with "confirm" or "cancel"
  kAbort = 115,       // to abort the current session
  kSuspend = 116,     // to suspend, so do save the context
  kRestore = 117,
  kUserInputEvent = 200
};

std::string ToString(const ConfUiCmd& cmd);
std::string ToDebugString(const ConfUiCmd& cmd, const bool is_debug);
ConfUiCmd ToCmd(const std::string& cmd_str);
ConfUiCmd ToCmd(std::uint32_t i);

struct UserResponse {
  using type = std::string;
  constexpr static const auto kConfirm = "user_confirm";
  constexpr static const auto kCancel = "user_cancel";
  constexpr static const auto kUnknown = "user_unknown";
};

// invalid/ignored session id
constexpr char SESSION_ANY[] = "";

std::string ToCliAckMessage(const bool is_success, const std::string& message);
std::string ToCliAckErrorMsg(const std::string& message);
std::string ToCliAckSuccessMsg(const std::string& message);

struct ConfUiMessage {
  std::string session_id_;
  std::string type_;  // cmd, which cmd? ack, response, etc
  std::string msg_;
};

}  // end of namespace confui
}  // end of namespace cuttlefish

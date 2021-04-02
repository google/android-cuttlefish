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

#include "common/libs/confui/protocol.h"

#include <map>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/confui/utils.h"

namespace cuttlefish {
namespace confui {
std::string ToDebugString(const ConfUiCmd& cmd, const bool is_debug) {
  std::stringstream ss;
  ss << "of " << Enum2Base(cmd);
  std::string suffix = "";
  if (is_debug) {
    suffix.append(ss.str());
  }
  static std::unordered_map<ConfUiCmd, std::string> look_up_tab{
      {ConfUiCmd::kUnknown, "kUnknown"},
      {ConfUiCmd::kStart, "kStart"},
      {ConfUiCmd::kStop, "kStop"},
      {ConfUiCmd::kCliAck, "kCliAck"},
      {ConfUiCmd::kCliRespond, "kCliRespond"},
      {ConfUiCmd::kAbort, "kAbort"},
      {ConfUiCmd::kSuspend, "kSuspend"},
      {ConfUiCmd::kRestore, "kRestore"},
      {ConfUiCmd::kUserInputEvent, "kUserInputEvent"}};
  if (look_up_tab.find(cmd) != look_up_tab.end()) {
    return look_up_tab[cmd] + suffix;
  }
  return "kUnknown" + suffix;
}

std::string ToString(const ConfUiCmd& cmd) { return ToDebugString(cmd, false); }

ConfUiCmd ToCmd(std::uint32_t i) {
  std::vector<ConfUiCmd> all_cmds{
      ConfUiCmd::kStart,      ConfUiCmd::kStop,           ConfUiCmd::kCliAck,
      ConfUiCmd::kCliRespond, ConfUiCmd::kAbort,          ConfUiCmd::kSuspend,
      ConfUiCmd::kRestore,    ConfUiCmd::kUserInputEvent, ConfUiCmd::kUnknown};

  for (auto& cmd : all_cmds) {
    if (i == Enum2Base(cmd)) {
      return cmd;
    }
  }
  return ConfUiCmd::kUnknown;
}

ConfUiCmd ToCmd(const std::string& cmd_str) {
  static std::map<std::string, ConfUiCmd> cmds = {
      {"kStart", ConfUiCmd::kStart},
      {"kStop", ConfUiCmd::kStop},
      {"kCliAck", ConfUiCmd::kCliAck},
      {"kCliRespond", ConfUiCmd::kCliRespond},
      {"kAbort", ConfUiCmd::kAbort},
      {"kSuspend", ConfUiCmd::kSuspend},
      {"kRestore", ConfUiCmd::kRestore},
      {"kUserInputEvent", ConfUiCmd::kUserInputEvent},
  };
  if (cmds.find(cmd_str) != cmds.end()) {
    return cmds[cmd_str];
  }
  return ConfUiCmd::kUnknown;
}

std::string ToCliAckMessage(const bool is_success, const std::string& message) {
  std::string header = "error:";
  if (is_success) {
    header = "success:";
  }
  return header + message;
}

std::string ToCliAckSuccessMsg(const std::string& message) {
  return ToCliAckMessage(true, message);
}

std::string ToCliAckErrorMsg(const std::string& message) {
  return ToCliAckMessage(false, message);
}

}  // end of namespace confui
}  // end of namespace cuttlefish

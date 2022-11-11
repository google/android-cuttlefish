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

#include "common/libs/confui/protocol_types.h"

#include <map>
#include <sstream>
#include <unordered_map>

#include "common/libs/confui/packet.h"
#include "common/libs/confui/utils.h"
#include "common/libs/utils/contains.h"

namespace cuttlefish {
namespace confui {
std::string ToDebugString(const ConfUiCmd& cmd, const bool is_verbose) {
  std::stringstream ss;
  ss << " of " << Enum2Base(cmd);
  std::string suffix = "";
  if (is_verbose) {
    suffix.append(ss.str());
  }
  static std::unordered_map<ConfUiCmd, std::string> look_up_tab{
      {ConfUiCmd::kUnknown, "kUnknown"},
      {ConfUiCmd::kStart, "kStart"},
      {ConfUiCmd::kStop, "kStop"},
      {ConfUiCmd::kCliAck, "kCliAck"},
      {ConfUiCmd::kCliRespond, "kCliRespond"},
      {ConfUiCmd::kAbort, "kAbort"},
      {ConfUiCmd::kUserInputEvent, "kUserInputEvent"},
      {ConfUiCmd::kUserInputEvent, "kUserTouchEvent"}};
  if (Contains(look_up_tab, cmd)) {
    return look_up_tab[cmd] + suffix;
  }
  return "kUnknown" + suffix;
}

std::string ToString(const ConfUiCmd& cmd) { return ToDebugString(cmd, false); }

ConfUiCmd ToCmd(std::uint32_t i) {
  std::vector<ConfUiCmd> all_cmds{
      ConfUiCmd::kStart,          ConfUiCmd::kStop,
      ConfUiCmd::kCliAck,         ConfUiCmd::kCliRespond,
      ConfUiCmd::kAbort,          ConfUiCmd::kUserInputEvent,
      ConfUiCmd::kUserTouchEvent, ConfUiCmd::kUnknown};

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
      {"kUserInputEvent", ConfUiCmd::kUserInputEvent},
      {"kUserTouchEvent", ConfUiCmd::kUserTouchEvent},
  };
  if (Contains(cmds, cmd_str)) {
    return cmds[cmd_str];
  }
  return ConfUiCmd::kUnknown;
}

std::string ToString(const teeui::UIOption ui_opt) {
  return std::to_string(static_cast<int>(ui_opt));
}

std::optional<teeui::UIOption> ToUiOption(const std::string& src) {
  if (!IsOnlyDigits(src)) {
    return std::nullopt;
  }
  return {static_cast<teeui::UIOption>(std::stoi(src))};
}

template <typename T>
static std::string ByteVecToString(const std::vector<T>& v) {
  static_assert(sizeof(T) == 1);
  std::string result{v.begin(), v.end()};
  return result;
}

bool ConfUiMessage::IsUserInput() const {
  switch (GetType()) {
    case ConfUiCmd::kUserInputEvent:
    case ConfUiCmd::kUserTouchEvent:
      return true;
    default:
      return false;
  }
}

std::string ConfUiAckMessage::ToString() const {
  return CreateString(session_id_, confui::ToString(GetType()),
                      (is_success_ ? "success" : "fail"), status_message_);
}

bool ConfUiAckMessage::SendOver(SharedFD fd) {
  return Send_(fd, GetType(), session_id_,
               std::string(is_success_ ? "success" : "fail"), status_message_);
}

std::string ConfUiCliResponseMessage::ToString() const {
  return CreateString(session_id_, confui::ToString(GetType()), response_,
                      ByteVecToString(sign_), ByteVecToString(message_));
}

bool ConfUiCliResponseMessage::SendOver(SharedFD fd) {
  return Send_(fd, GetType(), session_id_, response_, sign_, message_);
}

std::string ConfUiStartMessage::UiOptsToString() const {
  std::stringstream ss;
  for (const auto& ui_opt : ui_opts_) {
    ss << cuttlefish::confui::ToString(ui_opt) << ",";
  }
  auto ui_opt_str = ss.str();
  if (!ui_opt_str.empty()) {
    ui_opt_str.pop_back();
  }
  return ui_opt_str;
}

std::string ConfUiStartMessage::ToString() const {
  auto ui_opts_str = UiOptsToString();
  return CreateString(
      session_id_, confui::ToString(GetType()), prompt_text_, locale_,
      std::string(extra_data_.begin(), extra_data_.end()), ui_opts_str);
}

bool ConfUiStartMessage::SendOver(SharedFD fd) {
  return Send_(fd, GetType(), session_id_, prompt_text_, extra_data_, locale_,
               UiOptsToString());
}

std::string ConfUiUserSelectionMessage::ToString() const {
  return CreateString(session_id_, confui::ToString(GetType()), response_);
}

bool ConfUiUserSelectionMessage::SendOver(SharedFD fd) {
  return Send_(fd, GetType(), session_id_, response_);
}

std::string ConfUiUserTouchMessage::ToString() const {
  std::stringstream ss;
  ss << "(" << x_ << "," << y_ << ")";
  auto pos = ss.str();
  return CreateString(session_id_, confui::ToString(GetType()), response_, pos);
}

bool ConfUiUserTouchMessage::SendOver(SharedFD fd) {
  return Send_(fd, GetType(), session_id_, std::to_string(x_),
               std::to_string(y_));
}

}  // end of namespace confui
}  // end of namespace cuttlefish

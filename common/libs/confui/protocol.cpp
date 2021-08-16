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
#include "common/libs/fs/shared_buf.h"

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
std::string ToString(const ConfUiMessage& msg) {
  return "[" + msg.session_id_ + ", " + msg.type_ + ", " + msg.msg_ + "]";
}

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

std::optional<std::tuple<bool, std::string>> FromCliAckCmd(
    const std::string& message) {
  auto colon_pos = message.find_first_of(":");
  if (colon_pos == std::string::npos) {
    ConfUiLog(ERROR) << "Received message, \"" << message
                     << "\" is ill-formatted ";
    return std::nullopt;
  }
  std::string header = message.substr(0, colon_pos);
  std::string msg = message.substr(colon_pos + 1);
  if (header != "error" && header != "success") {
    ConfUiLog(ERROR) << "Received message, \"" << message
                     << "\" has a wrong header";
    return std::nullopt;
  }
  return {std::tuple{(header == "success"), msg}};
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

template <typename... Args>
bool WritePayload(SharedFD fd, const ConfUiCmd cmd,
                  const std::string& session_id, Args&&... args) {
  return packet::WritePayload(fd, session_id, ToString(cmd), session_id,
                              std::forward<Args>(args)...);
}

ConfUiMessage PayloadToConfUiMessage(const std::string& str_to_parse) {
  auto tokens = android::base::Split(str_to_parse, ":");
  ConfUiCheck(tokens.size() >= 3)
      << "PayloadToConfUiMessage takes \"" + str_to_parse + "\""
      << "and does not have 3 tokens";
  std::string msg;
  std::for_each(tokens.begin() + 2, tokens.end() - 1,
                [&msg](auto& token) { msg.append(token + ":"); });
  msg.append(*tokens.rbegin());
  return {tokens[0], tokens[1], msg};
}

static std::optional<ConfUiMessage> ReadPayload(SharedFD s) {
  if (!s->IsOpen()) {
    ConfUiLog(ERROR) << "file, socket, etc, is not open to read";
    return std::nullopt;
  }
  auto payload_read = packet::ReadPayload(s);
  if (!payload_read) {
    return std::nullopt;
  }
  auto msg_to_parse = payload_read.value();
  auto [session_id, type, contents] = PayloadToConfUiMessage(msg_to_parse);
  return {{session_id, type, contents}};
}

std::optional<ConfUiMessage> RecvConfUiMsg(SharedFD fd) {
  return ReadPayload(fd);
}

std::optional<std::tuple<bool, std::string>> RecvAck(
    SharedFD fd, const std::string& session_id) {
  auto conf_ui_msg = RecvConfUiMsg(fd);
  if (!conf_ui_msg) {
    ConfUiLog(ERROR) << "Received Ack failed due to communication.";
    return std::nullopt;
  }
  auto [recv_session_id, type, contents] = conf_ui_msg.value();
  if (session_id != recv_session_id) {
    ConfUiLog(ERROR) << "Received Session ID is not the expected one,"
                     << session_id;
    return std::nullopt;
  }
  if (ToCmd(type) != ConfUiCmd::kCliAck) {
    ConfUiLog(ERROR) << "Received cmd is not ack but " << type;
    return std::nullopt;
  }
  return FromCliAckCmd(contents);
}

bool SendAck(SharedFD fd, const std::string& session_id, const bool is_success,
             const std::string& additional_info) {
  return SendCmd(fd, session_id, ConfUiCmd::kCliAck,
                 ToCliAckMessage(is_success, additional_info));
}

bool SendResponse(SharedFD fd, const std::string& session_id,
                  const std::string& additional_info) {
  return WritePayload(fd, ConfUiCmd::kCliRespond, session_id, additional_info);
}

bool SendCmd(SharedFD fd, const std::string& session_id, ConfUiCmd cmd,
             const std::string& additional_info) {
  return WritePayload(fd, cmd, session_id, additional_info);
}

}  // end of namespace confui
}  // end of namespace cuttlefish

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

#include <sstream>
#include <vector>

#include <android-base/strings.h>

#include "common/libs/confui/packet.h"
#include "common/libs/confui/utils.h"
#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {
namespace confui {
namespace {
// default implementation of ToConfUiMessage
template <ConfUiCmd C>
std::unique_ptr<ConfUiMessage> ToConfUiMessage(
    const packet::ParsedPacket& message) {
  return std::make_unique<ConfUiGenericMessage<C>>(message.session_id_);
}

// these are specialized, and defined below
template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kCliAck>(
    const packet::ParsedPacket& message);
template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kStart>(
    const packet::ParsedPacket& message);
template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kUserInputEvent>(
    const packet::ParsedPacket& message);
template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kUserTouchEvent>(
    const packet::ParsedPacket& message);
template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kCliRespond>(
    const packet::ParsedPacket& message);

std::unique_ptr<ConfUiMessage> ToConfUiMessage(
    const packet::ParsedPacket& confui_packet) {
  const auto confui_cmd = ToCmd(confui_packet.type_);
  switch (confui_cmd) {
    // customized ConfUiMessage
    case ConfUiCmd::kStart:
      return ToConfUiMessage<ConfUiCmd::kStart>(confui_packet);
    case ConfUiCmd::kCliAck:
      return ToConfUiMessage<ConfUiCmd::kCliAck>(confui_packet);
    case ConfUiCmd::kCliRespond:
      return ToConfUiMessage<ConfUiCmd::kCliRespond>(confui_packet);
    case ConfUiCmd::kUserInputEvent:
      return ToConfUiMessage<ConfUiCmd::kUserInputEvent>(confui_packet);
    case ConfUiCmd::kUserTouchEvent:
      return ToConfUiMessage<ConfUiCmd::kUserTouchEvent>(confui_packet);
      // default ConfUiMessage with session & type only
    case ConfUiCmd::kAbort:
      return ToConfUiMessage<ConfUiCmd::kAbort>(confui_packet);
    case ConfUiCmd::kStop:
      return ToConfUiMessage<ConfUiCmd::kStop>(confui_packet);
      // these are errors
    case ConfUiCmd::kUnknown:
    default:
      ConfUiLog(ERROR) << "ConfUiCmd value is not good for ToConfUiMessage: "
                       << ToString(confui_cmd);
      break;
  }
  return {nullptr};
}
}  // end of unnamed namespace

std::string ToString(const ConfUiMessage& msg) { return msg.ToString(); }

std::unique_ptr<ConfUiMessage> RecvConfUiMsg(SharedFD fd) {
  if (!fd->IsOpen()) {
    ConfUiLog(ERROR) << "file, socket, etc, is not open to read";
    return {nullptr};
  }
  auto confui_packet_opt = packet::ReadPayload(fd);
  if (!confui_packet_opt) {
    ConfUiLog(ERROR) << "ReadPayload returns but with std::nullptr";
    return {nullptr};
  }

  auto confui_packet = confui_packet_opt.value();
  return ToConfUiMessage(confui_packet);
}

std::unique_ptr<ConfUiMessage> RecvConfUiMsg(const std::string& session_id,
                                             SharedFD fd) {
  auto conf_ui_msg = RecvConfUiMsg(fd);
  if (!conf_ui_msg) {
    return {nullptr};
  }
  auto recv_session_id = conf_ui_msg->GetSessionId();
  if (session_id != recv_session_id) {
    ConfUiLog(ERROR) << "Received Session ID (" << recv_session_id
                     << ") is not the expected one (" << session_id << ")";
    return {nullptr};
  }
  return conf_ui_msg;
}

bool SendAbortCmd(SharedFD fd, const std::string& session_id) {
  ConfUiGenericMessage<ConfUiCmd::kAbort> confui_msg{session_id};
  return confui_msg.SendOver(fd);
}

bool SendStopCmd(SharedFD fd, const std::string& session_id) {
  ConfUiGenericMessage<ConfUiCmd::kStop> confui_msg{session_id};
  return confui_msg.SendOver(fd);
}

bool SendAck(SharedFD fd, const std::string& session_id, const bool is_success,
             const std::string& status_message) {
  ConfUiAckMessage confui_msg{session_id, is_success, status_message};
  return confui_msg.SendOver(fd);
}

bool SendResponse(SharedFD fd, const std::string& session_id,
                  const UserResponse::type& plain_selection,
                  const std::vector<std::uint8_t>& signed_response,
                  const std::vector<std::uint8_t>& message) {
  ConfUiCliResponseMessage confui_msg{session_id, plain_selection,
                                      signed_response, message};
  return confui_msg.SendOver(fd);
}

bool SendStartCmd(SharedFD fd, const std::string& session_id,
                  const std::string& prompt_text,
                  const std::vector<std::uint8_t>& extra_data,
                  const std::string& locale,
                  const std::vector<teeui::UIOption>& ui_opts) {
  ConfUiStartMessage confui_msg{session_id, prompt_text, extra_data, locale,
                                ui_opts};
  return confui_msg.SendOver(fd);
}

// this is only for deliverSecureInputEvent
bool SendUserSelection(SharedFD fd, const std::string& session_id,
                       const UserResponse::type& confirm_cancel) {
  ConfUiUserSelectionMessage confui_msg{session_id, confirm_cancel};
  return confui_msg.SendOver(fd);
}

// specialized ToConfUiMessage()
namespace {
template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kCliAck>(
    const packet::ParsedPacket& message) {
  auto type = ToCmd(message.type_);
  auto& contents = message.additional_info_;
  if (type != ConfUiCmd::kCliAck) {
    ConfUiLog(ERROR) << "Received cmd is not ack but " << ToString(type);
    return {nullptr};
  }

  if (contents.size() != 2) {
    ConfUiLog(ERROR)
        << "Ack message should only have pass/fail and a status message";
    return {nullptr};
  }

  const std::string success_str(contents[0].begin(), contents[0].end());
  const bool is_success = (success_str == "success");
  const std::string status_message(contents[1].begin(), contents[1].end());
  return std::make_unique<ConfUiAckMessage>(message.session_id_, is_success,
                                            status_message);
}

template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kStart>(
    const packet::ParsedPacket& message) {
  /*
   * additional_info_[0]: prompt text
   * additional_info_[1]: extra data
   * additional_info_[2]: locale
   * additional_info_[3]: UIOptions
   *
   */
  if (message.additional_info_.size() < 3) {
    ConfUiLog(ERROR) << "ConfUiMessage for kStart is ill-formatted: "
                     << packet::ToString(message);
    return {nullptr};
  }
  std::vector<teeui::UIOption> ui_opts;
  bool has_ui_option = (message.additional_info_.size() == 4) &&
                       !(message.additional_info_[3].empty());
  if (has_ui_option) {
    std::string ui_opts_string{message.additional_info_[3].begin(),
                               message.additional_info_[3].end()};
    auto tokens = android::base::Split(ui_opts_string, ",");
    for (auto token : tokens) {
      auto ui_opt_optional = ToUiOption(token);
      if (!ui_opt_optional) {
        ConfUiLog(ERROR) << "Wrong UiOption String : " << token;
        return {nullptr};
      }
      ui_opts.emplace_back(ui_opt_optional.value());
    }
  }
  auto sm = std::make_unique<ConfUiStartMessage>(
      message.session_id_,
      std::string(message.additional_info_[0].begin(),
                  message.additional_info_[0].end()),
      message.additional_info_[1],
      std::string(message.additional_info_[2].begin(),
                  message.additional_info_[2].end()),
      ui_opts);
  return sm;
}

template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kUserInputEvent>(
    const packet::ParsedPacket& message) {
  if (message.additional_info_.size() < 1) {
    ConfUiLog(ERROR)
        << "kUserInputEvent message should have at least one additional_info_";
    return {nullptr};
  }
  auto response = std::string{message.additional_info_[0].begin(),
                              message.additional_info_[0].end()};
  return std::make_unique<ConfUiUserSelectionMessage>(message.session_id_,
                                                      response);
}

template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kUserTouchEvent>(
    const packet::ParsedPacket& message) {
  if (message.additional_info_.size() < 2) {
    ConfUiLog(ERROR)
        << "kUserTouchEvent message should have at least two additional_info_";
    return {nullptr};
  }
  auto x = std::string(message.additional_info_[0].begin(),
                       message.additional_info_[0].end());
  auto y = std::string(message.additional_info_[1].begin(),
                       message.additional_info_[1].end());
  return std::make_unique<ConfUiUserTouchMessage>(message.session_id_,
                                                  std::stoi(x), std::stoi(y));
}

template <>
std::unique_ptr<ConfUiMessage> ToConfUiMessage<ConfUiCmd::kCliRespond>(
    const packet::ParsedPacket& message) {
  if (message.additional_info_.size() < 3) {
    ConfUiLog(ERROR)
        << "kCliRespond message should have at least two additional info";
    return {nullptr};
  }
  auto response = std::string{message.additional_info_[0].begin(),
                              message.additional_info_[0].end()};
  auto sign = message.additional_info_[1];
  auto msg = message.additional_info_[2];
  return std::make_unique<ConfUiCliResponseMessage>(message.session_id_,
                                                    response, sign, msg);
}
}  // end of unnamed namespace
}  // end of namespace confui
}  // end of namespace cuttlefish

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
#include <utility>
#include <vector>

#include <teeui/common_message_types.h>  // /system/teeui/libteeui/.../include

#include "common/libs/confui/packet.h"
#include "common/libs/confui/packet_types.h"

#include "common/libs/confui/utils.h"
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace confui {
// When you update this, please update all the utility functions
// in conf.cpp: e.g. ToString, etc
enum class ConfUiCmd : std::uint32_t {
  kUnknown = 100,
  kStart = 111,   // start rendering, send confirmation msg, & wait respond
  kStop = 112,    // start rendering, send confirmation msg, & wait respond
  kCliAck = 113,  // client acknowledged. "error:err_msg" or "success:command"
  kCliRespond = 114,  //  with "confirm" or "cancel" or "abort"
  kAbort = 115,       // to abort the current session
  kUserInputEvent = 200,
  kUserTouchEvent = 201
};

// this is for short messages
constexpr const ssize_t kMaxMessageLength = packet::kMaxPayloadLength;

std::string ToString(const ConfUiCmd& cmd);
std::string ToDebugString(const ConfUiCmd& cmd, const bool is_debug);
ConfUiCmd ToCmd(const std::string& cmd_str);
ConfUiCmd ToCmd(std::uint32_t i);

std::string ToString(const teeui::UIOption ui_opt);
std::optional<teeui::UIOption> ToUiOption(const std::string&);

struct HostError {
  static constexpr char kSystemError[] = "system_error";
  static constexpr char kUIError[] = "ui_error";
  static constexpr char kMessageTooLongError[] = "msg_too_long_error";
  static constexpr char kIncorrectUTF8[] = "msg_incorrect_utf8";
};

struct UserResponse {
  using type = std::string;
  constexpr static const auto kConfirm = "user_confirm";
  constexpr static const auto kCancel = "user_cancel";
  constexpr static const auto kTouchEvent = "user_touch";
  // user may close x button on the virtual window or so
  // or.. scroll the session up and throw to trash bin
  constexpr static const auto kUserAbort = "user_abort";
  constexpr static const auto kUnknown = "user_unknown";
};

class ConfUiMessage {
 public:
  ConfUiMessage(const std::string& session_id) : session_id_{session_id} {}
  virtual ~ConfUiMessage() = default;
  virtual std::string ToString() const = 0;
  void SetSessionId(const std::string session_id) { session_id_ = session_id; }
  std::string GetSessionId() const { return session_id_; }
  virtual ConfUiCmd GetType() const = 0;
  virtual bool SendOver(SharedFD fd) = 0;
  bool IsUserInput() const;

 protected:
  std::string session_id_;
  template <typename... Args>
  static std::string CreateString(Args&&... args) {
    return "[" + ArgsToStringWithDelim(",", std::forward<Args>(args)...) + "]";
  }
  template <typename... Args>
  static bool Send_(SharedFD fd, const ConfUiCmd cmd,
                    const std::string& session_id, Args&&... args) {
    return packet::WritePayload(fd, confui::ToString(cmd), session_id,
                                std::forward<Args>(args)...);
  }
};

template <ConfUiCmd cmd>
class ConfUiGenericMessage : public ConfUiMessage {
 public:
  ConfUiGenericMessage(const std::string& session_id)
      : ConfUiMessage{session_id} {}
  virtual ~ConfUiGenericMessage() = default;
  std::string ToString() const override {
    return CreateString(session_id_, confui::ToString(GetType()));
  }
  ConfUiCmd GetType() const override { return cmd; }
  bool SendOver(SharedFD fd) override {
    return Send_(fd, GetType(), session_id_);
  }
};

class ConfUiAckMessage : public ConfUiMessage {
 public:
  ConfUiAckMessage(const std::string& session_id, const bool is_success,
                   const std::string& status)
      : ConfUiMessage{session_id},
        is_success_(is_success),
        status_message_(status) {}
  virtual ~ConfUiAckMessage() = default;
  std::string ToString() const override;
  ConfUiCmd GetType() const override { return ConfUiCmd::kCliAck; }
  bool SendOver(SharedFD fd) override;
  bool IsSuccess() const { return is_success_; }
  std::string GetStatusMessage() const { return status_message_; }

 private:
  bool is_success_;
  std::string status_message_;
};

// the signed user response sent to the guest
class ConfUiCliResponseMessage : public ConfUiMessage {
 public:
  ConfUiCliResponseMessage(const std::string& session_id,
                           const UserResponse::type& response,
                           const std::vector<std::uint8_t>& sign = {},
                           const std::vector<std::uint8_t>& msg = {})
      : ConfUiMessage(session_id),
        response_(response),
        sign_(sign),
        message_{msg} {}
  virtual ~ConfUiCliResponseMessage() = default;
  std::string ToString() const override;
  ConfUiCmd GetType() const override { return ConfUiCmd::kCliRespond; }
  auto GetResponse() const { return response_; }
  auto GetMessage() const { return message_; }
  auto GetSign() const { return sign_; }
  bool SendOver(SharedFD fd) override;

 private:
  UserResponse::type response_;     // plain format
  std::vector<std::uint8_t> sign_;  // signed format
  // second argument to pass via resultCB of promptUserConfirmation
  std::vector<std::uint8_t> message_;
};

class ConfUiStartMessage : public ConfUiMessage {
 public:
  ConfUiStartMessage(const std::string session_id,
                     const std::string& prompt_text = "",
                     const std::vector<std::uint8_t>& extra_data = {},
                     const std::string& locale = "C",
                     const std::vector<teeui::UIOption> ui_opts = {})
      : ConfUiMessage(session_id),
        prompt_text_(prompt_text),
        extra_data_(extra_data),
        locale_(locale),
        ui_opts_(ui_opts) {}
  virtual ~ConfUiStartMessage() = default;
  std::string ToString() const override;
  ConfUiCmd GetType() const override { return ConfUiCmd::kStart; }
  std::string GetPromptText() const { return prompt_text_; }
  std::vector<std::uint8_t> GetExtraData() const { return extra_data_; }
  std::string GetLocale() const { return locale_; }
  std::vector<teeui::UIOption> GetUiOpts() const { return ui_opts_; }
  bool SendOver(SharedFD fd) override;

 private:
  std::string prompt_text_;
  std::vector<std::uint8_t> extra_data_;
  std::string locale_;
  std::vector<teeui::UIOption> ui_opts_;

  std::string UiOptsToString() const;
};

// this one is for deliverSecureInputEvent() as well as
// physical-input based implementation
class ConfUiUserSelectionMessage : public ConfUiMessage {
 public:
  ConfUiUserSelectionMessage(const std::string& session_id,
                             const UserResponse::type& response)
      : ConfUiMessage(session_id), response_(response) {}
  virtual ~ConfUiUserSelectionMessage() = default;
  std::string ToString() const override;
  ConfUiCmd GetType() const override { return ConfUiCmd::kUserInputEvent; }
  auto GetResponse() const { return response_; }
  bool SendOver(SharedFD fd) override;

 private:
  UserResponse::type response_;
};

class ConfUiUserTouchMessage : public ConfUiMessage {
 public:
  ConfUiUserTouchMessage(const std::string& session_id, const int x,
                         const int y)
      : ConfUiMessage(session_id),
        x_(x),
        y_(y),
        response_(UserResponse::kTouchEvent) {}
  virtual ~ConfUiUserTouchMessage() = default;
  std::string ToString() const override;
  ConfUiCmd GetType() const override { return ConfUiCmd::kUserTouchEvent; }
  auto GetResponse() const { return response_; }
  bool SendOver(SharedFD fd) override;
  std::pair<int, int> GetLocation() const { return {x_, y_}; }

 private:
  int x_;
  int y_;
  UserResponse::type response_;
};

using ConfUiAbortMessage = ConfUiGenericMessage<ConfUiCmd::kAbort>;
using ConfUiStopMessage = ConfUiGenericMessage<ConfUiCmd::kStop>;

}  // end of namespace confui
}  // end of namespace cuttlefish

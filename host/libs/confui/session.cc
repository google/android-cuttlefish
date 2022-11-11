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

#include "host/libs/confui/session.h"

#include <algorithm>

#include "common/libs/utils/contains.h"
#include "host/libs/confui/secure_input.h"

namespace cuttlefish {
namespace confui {

Session::Session(const std::string& session_name,
                 const std::uint32_t display_num, HostModeCtrl& host_mode_ctrl,
                 ScreenConnectorFrameRenderer& screen_connector,
                 const std::string& locale)
    : session_id_{session_name},
      display_num_{display_num},
      host_mode_ctrl_{host_mode_ctrl},
      screen_connector_{screen_connector},
      locale_{locale},
      state_{MainLoopState::kInit},
      saved_state_{MainLoopState::kInit} {}

/** return grace period + alpha
 *
 * grace period is the gap between user seeing the dialog
 * and the UI starts to take the user inputs
 * Grace period should be at least 1s.
 * Session requests the Renderer to render the dialog,
 * but it might not be immediate. So, add alpha to 1s
 */
static const std::chrono::milliseconds GetGracePeriod() {
  using std::literals::chrono_literals::operator""ms;
  return 1000ms + 100ms;
}

bool Session::IsReadyForUserInput() const {
  using std::literals::chrono_literals::operator""ms;
  if (!start_time_) {
    return false;
  }
  const auto right_now = Clock::now();
  return (right_now - *start_time_) >= GetGracePeriod();
}

bool Session::IsConfUiActive() const {
  if (state_ == MainLoopState::kInSession ||
      state_ == MainLoopState::kWaitStop) {
    return true;
  }
  return false;
}

bool Session::IsInverted() const {
  return Contains(ui_options_, teeui::UIOption::AccessibilityInverted);
}

bool Session::IsMagnified() const {
  return Contains(ui_options_, teeui::UIOption::AccessibilityMagnified);
}

bool Session::RenderDialog() {
  renderer_ = ConfUiRenderer::GenerateRenderer(
      display_num_, prompt_text_, locale_, IsInverted(), IsMagnified());
  if (!renderer_) {
    return false;
  }
  auto teeui_frame = renderer_->RenderRawFrame();
  if (!teeui_frame) {
    return false;
  }
  ConfUiLog(VERBOSE) << "actually trying to render the frame"
                     << thread::GetName();
  auto frame_width = teeui_frame->Width();
  auto frame_height = teeui_frame->Height();
  auto frame_stride_bytes = teeui_frame->ScreenStrideBytes();
  auto frame_bytes = reinterpret_cast<std::uint8_t*>(teeui_frame->data());
  return screen_connector_.RenderConfirmationUi(
      display_num_, frame_width, frame_height, frame_stride_bytes, frame_bytes);
}

MainLoopState Session::Transition(SharedFD& hal_cli, const FsmInput fsm_input,
                                  const ConfUiMessage& conf_ui_message) {
  bool should_keep_running = false;
  bool already_terminated = false;
  switch (state_) {
    case MainLoopState::kInit: {
      should_keep_running = HandleInit(hal_cli, fsm_input, conf_ui_message);
    } break;
    case MainLoopState::kInSession: {
      should_keep_running =
          HandleInSession(hal_cli, fsm_input, conf_ui_message);
    } break;
    case MainLoopState::kWaitStop: {
      if (IsUserInput(fsm_input)) {
        ConfUiLog(VERBOSE) << "User input ignored " << ToString(fsm_input)
                           << " : " << ToString(conf_ui_message)
                           << " at the state " << ToString(state_);
      }
      should_keep_running = HandleWaitStop(hal_cli, fsm_input);
    } break;
    case MainLoopState::kTerminated: {
      already_terminated = true;
    } break;
    default:
      ConfUiLog(FATAL) << "Must not be in the state of " << ToString(state_);
      break;
  }
  if (!should_keep_running && !already_terminated) {
    ScheduleToTerminate();
  }
  return state_;
};

void Session::CleanUp() {
  if (state_ != MainLoopState::kAwaitCleanup) {
    ConfUiLog(FATAL) << "Clean up a session only when in kAwaitCleanup";
  }
  state_ = MainLoopState::kTerminated;
  // common action done when the state is back to init state
  host_mode_ctrl_.SetMode(HostModeCtrl::ModeType::kAndroidMode);
}

void Session::ScheduleToTerminate() {
  state_ = MainLoopState::kAwaitCleanup;
  saved_state_ = MainLoopState::kInvalid;
}

bool Session::ReportErrorToHal(SharedFD hal_cli, const std::string& msg) {
  ScheduleToTerminate();
  if (!SendAck(hal_cli, session_id_, false, msg)) {
    ConfUiLog(ERROR) << "I/O error in sending ack to report rendering failure";
    return false;
  }
  return true;
}

void Session::Abort() {
  ConfUiLog(VERBOSE) << "Abort is called";
  ScheduleToTerminate();
  return;
}

void Session::UserAbort(SharedFD hal_cli) {
  ConfUiLog(VERBOSE) << "it is a user abort input.";
  SendAbortCmd(hal_cli, GetId());
  Abort();
  ScheduleToTerminate();
}

bool Session::HandleInit(SharedFD hal_cli, const FsmInput fsm_input,
                         const ConfUiMessage& conf_ui_message) {
  if (IsUserInput(fsm_input)) {
    // ignore user input
    state_ = MainLoopState::kInit;
    return true;
  }

  ConfUiLog(VERBOSE) << ToString(fsm_input) << "is handled in HandleInit";
  if (fsm_input != FsmInput::kHalStart) {
    ConfUiLog(ERROR) << "invalid cmd for Init State:" << ToString(fsm_input);
    // ReportErrorToHal returns true if error report was successful
    // However, anyway we abort this session on the host
    ReportErrorToHal(hal_cli, HostError::kSystemError);
    return false;
  }

  // Start Session
  ConfUiLog(VERBOSE) << "Sending ack to hal_cli: "
                     << Enum2Base(ConfUiCmd::kCliAck);
  host_mode_ctrl_.SetMode(HostModeCtrl::ModeType::kConfUI_Mode);

  auto start_cmd_msg = static_cast<const ConfUiStartMessage&>(conf_ui_message);
  prompt_text_ = start_cmd_msg.GetPromptText();
  locale_ = start_cmd_msg.GetLocale();
  extra_data_ = start_cmd_msg.GetExtraData();
  ui_options_ = start_cmd_msg.GetUiOpts();

  // cbor_ can be correctly created after the session received kStart cmd
  // at runtime
  cbor_ = std::make_unique<Cbor>(prompt_text_, extra_data_);
  if (cbor_->IsMessageTooLong()) {
    ConfUiLog(ERROR) << "The prompt text and extra_data are too long to be "
                     << "properly encoded.";
    ReportErrorToHal(hal_cli, HostError::kMessageTooLongError);
    return false;
  }
  if (cbor_->IsMalformedUtf8()) {
    ConfUiLog(ERROR) << "The prompt text appears to have incorrect UTF8 format";
    ReportErrorToHal(hal_cli, HostError::kIncorrectUTF8);
    return false;
  }
  if (!cbor_->IsOk()) {
    ConfUiLog(ERROR) << "Unknown Error in cbor implementation";
    ReportErrorToHal(hal_cli, HostError::kSystemError);
    return false;
  }

  if (!RenderDialog()) {
    // the confirmation UI is driven by a user app, not running from the start
    // automatically so that means webRTC should have been set up
    ConfUiLog(ERROR) << "Dialog is not rendered. However, it should."
                     << "No webRTC can't initiate any confirmation UI.";
    ReportErrorToHal(hal_cli, HostError::kUIError);
    return false;
  }
  start_time_ = std::make_unique<TimePoint>(std::move(Clock::now()));
  if (!SendAck(hal_cli, session_id_, true, "started")) {
    ConfUiLog(ERROR) << "Ack to kStart failed in I/O";
    return false;
  }
  state_ = MainLoopState::kInSession;
  return true;
}

bool Session::HandleInSession(SharedFD hal_cli, const FsmInput fsm_input,
                              const ConfUiMessage& conf_ui_msg) {
  auto invalid_input_handler = [&, this]() {
    ReportErrorToHal(hal_cli, HostError::kSystemError);
    ConfUiLog(ERROR) << "cmd " << ToString(fsm_input)
                     << " should not be handled in HandleInSession";
  };

  if (!IsUserInput(fsm_input)) {
    invalid_input_handler();
    return false;
  }

  const auto& user_input_msg =
      static_cast<const ConfUiSecureUserSelectionMessage&>(conf_ui_msg);
  const auto response = user_input_msg.GetResponse();
  if (response == UserResponse::kUnknown ||
      response == UserResponse::kUserAbort) {
    invalid_input_handler();
    return false;
  }
  const bool is_secure_input = user_input_msg.IsSecure();

  ConfUiLog(VERBOSE) << "In HandleInSession, session " << session_id_
                     << " is sending the user input " << ToString(fsm_input);

  bool is_success = false;
  if (response == UserResponse::kCancel) {
    // no need to sign
    is_success =
        SendResponse(hal_cli, session_id_, UserResponse::kCancel,
                     std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{});
  } else {
    message_ = std::move(cbor_->GetMessage());
    auto message_opt = (is_secure_input ? Sign(message_) : TestSign(message_));
    if (!message_opt) {
      ReportErrorToHal(hal_cli, HostError::kSystemError);
      return false;
    }
    signed_confirmation_ = message_opt.value();
    is_success = SendResponse(hal_cli, session_id_, UserResponse::kConfirm,
                              signed_confirmation_, message_);
  }

  if (!is_success) {
    ConfUiLog(ERROR) << "I/O error in sending user response to HAL";
    return false;
  }
  state_ = MainLoopState::kWaitStop;
  return true;
}

bool Session::HandleWaitStop(SharedFD hal_cli, const FsmInput fsm_input) {
  if (IsUserInput(fsm_input)) {
    // ignore user input
    state_ = MainLoopState::kWaitStop;
    return true;
  }
  if (fsm_input == FsmInput::kHalStop) {
    ConfUiLog(VERBOSE) << "Handling Abort in kWaitStop.";
    ScheduleToTerminate();
    return true;
  }
  ReportErrorToHal(hal_cli, HostError::kSystemError);
  ConfUiLog(FATAL) << "In WaitStop, received wrong HAL command "
                   << ToString(fsm_input);
  return false;
}

}  // end of namespace confui
}  // end of namespace cuttlefish

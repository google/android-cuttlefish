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

namespace cuttlefish {
namespace confui {

Session::Session(const std::string& session_id, const std::uint32_t display_num,
                 ConfUiRenderer& host_renderer, HostModeCtrl& host_mode_ctrl,
                 ScreenConnectorFrameRenderer& screen_connector,
                 const std::string& locale)
    : session_id_{session_id},
      display_num_{display_num},
      renderer_{host_renderer},
      host_mode_ctrl_{host_mode_ctrl},
      screen_connector_{screen_connector},
      locale_{locale},
      state_{MainLoopState::kInit},
      saved_state_{MainLoopState::kInit} {}

bool Session::IsConfUiActive() const {
  if (state_ == MainLoopState::kInSession ||
      state_ == MainLoopState::kWaitStop) {
    return true;
  }
  return false;
}

bool Session::RenderDialog(const std::string& msg, const std::string& locale) {
  auto [teeui_frame, is_success] = renderer_.RenderRawFrame(msg, locale);
  if (!is_success) {
    return false;
  }
  prompt_ = msg;
  locale_ = locale;

  ConfUiLog(DEBUG) << "actually trying to render the frame"
                   << thread::GetName();
  auto frame_width = ScreenConnectorInfo::ScreenWidth(display_num_);
  auto frame_height = ScreenConnectorInfo::ScreenHeight(display_num_);
  auto frame_stride_bytes =
      ScreenConnectorInfo::ScreenStrideBytes(display_num_);
  auto frame_bytes = reinterpret_cast<std::uint8_t*>(teeui_frame.data());
  return screen_connector_.RenderConfirmationUi(
      display_num_, frame_width, frame_height, frame_stride_bytes, frame_bytes);
}

bool Session::IsSuspended() const {
  return (state_ == MainLoopState::kSuspended);
}

MainLoopState Session::Transition(const bool is_user_input, SharedFD& hal_cli,
                                  const FsmInput fsm_input,
                                  const std::string& additional_info) {
  switch (state_) {
    case MainLoopState::kInit: {
      HandleInit(is_user_input, hal_cli, fsm_input, additional_info);
    } break;
    case MainLoopState::kInSession: {
      HandleInSession(is_user_input, hal_cli, fsm_input);
    } break;
    case MainLoopState::kWaitStop: {
      if (is_user_input) {
        ConfUiLog(DEBUG) << "User input ignored" << ToString(fsm_input) << " : "
                         << additional_info << "at state" << ToString(state_);
      }
      HandleWaitStop(is_user_input, hal_cli, fsm_input);
    } break;
    default:
      // host service explicitly calls restore and suspend
      ConfUiLog(FATAL) << "Must not be in the state of" << ToString(state_);
      break;
  }
  return state_;
};

bool Session::Suspend(SharedFD hal_cli) {
  if (state_ == MainLoopState::kInit) {
    // HAL sent wrong command
    ConfUiLog(FATAL)
        << "HAL sent wrong command, suspend, when the session is in kIinit";
    return false;
  }
  if (state_ == MainLoopState::kSuspended) {
    ConfUiLog(DEBUG) << "Already kSuspended state";
    return false;
  }
  saved_state_ = state_;
  state_ = MainLoopState::kSuspended;
  host_mode_ctrl_.SetMode(HostModeCtrl::ModeType::kAndroidMode);
  if (!packet::SendAck(hal_cli, session_id_, /*is success*/ true,
                       "suspended")) {
    ConfUiLog(FATAL) << "I/O error";
    return false;
  }
  return true;
}

bool Session::Restore(SharedFD hal_cli) {
  if (state_ == MainLoopState::kInit) {
    // HAL sent wrong command
    ConfUiLog(FATAL)
        << "HAL sent wrong command, restore, when the session is in kIinit";
    return false;
  }

  if (state_ != MainLoopState::kSuspended) {
    ConfUiLog(DEBUG) << "Already Restored to state " + ToString(state_);
    return false;
  }
  host_mode_ctrl_.SetMode(HostModeCtrl::ModeType::kConfUI_Mode);
  if (!RenderDialog(prompt_, locale_)) {
    // the confirmation UI is driven by a user app, not running from the start
    // automatically so that means webRTC/vnc should have been set up
    ConfUiLog(ERROR) << "Dialog is not rendered. However, it should."
                     << "No webRTC can't initiate any confirmation UI.";
    if (!packet::SendAck(hal_cli, session_id_, false,
                         "render failed in restore")) {
      ConfUiLog(FATAL) << "Rendering failed in restore, and ack failed in I/O";
    }
    state_ = MainLoopState::kInit;
    return false;
  }
  if (!packet::SendAck(hal_cli, session_id_, true, "restored")) {
    ConfUiLog(FATAL) << "Ack to restore failed in I/O";
  }
  state_ = saved_state_;
  saved_state_ = MainLoopState::kInit;
  return true;
}

bool Session::Kill(SharedFD hal_cli, const std::string& response_msg) {
  state_ = MainLoopState::kAwaitCleanup;
  saved_state_ = MainLoopState::kInvalid;
  if (!packet::SendAck(hal_cli, session_id_, true, response_msg)) {
    ConfUiLog(FATAL) << "I/O error in ack to Abort";
    return false;
  }
  return true;
}

void Session::CleanUp() {
  if (state_ != MainLoopState::kAwaitCleanup) {
    ConfUiLog(FATAL) << "Clean up a session only when in kAwaitCleanup";
  }
  // common action done when the state is back to init state
  host_mode_ctrl_.SetMode(HostModeCtrl::ModeType::kAndroidMode);
}

void Session::ReportErrorToHal(SharedFD hal_cli, const std::string& msg) {
  // reset the session -- destroy it & recreate it with the same
  // session id
  state_ = MainLoopState::kAwaitCleanup;
  if (!packet::SendAck(hal_cli, session_id_, false, msg)) {
    ConfUiLog(FATAL) << "I/O error in sending ack to report rendering failure";
  }
  return;
}

bool Session::Abort(SharedFD hal_cli) { return Kill(hal_cli, "aborted"); }

void Session::HandleInit(const bool is_user_input, SharedFD hal_cli,
                         const FsmInput fsm_input,
                         const std::string& additional_info) {
  using namespace cuttlefish::confui::packet;
  if (is_user_input) {
    // ignore user input
    state_ = MainLoopState::kInit;
    return;
  }

  ConfUiLog(DEBUG) << ToString(fsm_input) << "is handled in HandleInit";
  if (fsm_input != FsmInput::kHalStart) {
    ConfUiLog(ERROR) << "invalid cmd for Init State:" << ToString(fsm_input);
    // reset the session -- destroy it & recreate it with the same
    // session id
    ReportErrorToHal(hal_cli, "wrong hal command");
    return;
  }

  // Start Session
  ConfUiLog(DEBUG) << "Sending ack to hal_cli: "
                   << Enum2Base(ConfUiCmd::kCliAck);
  host_mode_ctrl_.SetMode(HostModeCtrl::ModeType::kConfUI_Mode);
  auto confirmation_msg = additional_info;
  if (!RenderDialog(confirmation_msg, locale_)) {
    // the confirmation UI is driven by a user app, not running from the start
    // automatically so that means webRTC/vnc should have been set up
    ConfUiLog(ERROR) << "Dialog is not rendered. However, it should."
                     << "No webRTC can't initiate any confirmation UI.";
    ReportErrorToHal(hal_cli, "rendering failed");
    return;
  }
  if (!packet::SendAck(hal_cli, session_id_, true, "started")) {
    ConfUiLog(FATAL) << "Ack to kStart failed in I/O";
  }
  state_ = MainLoopState::kInSession;
  return;
}

void Session::HandleInSession(const bool is_user_input, SharedFD hal_cli,
                              const FsmInput fsm_input) {
  if (!is_user_input) {
    ConfUiLog(FATAL) << "cmd" << ToString(fsm_input)
                     << "should not be handled in HandleInSession";
    ReportErrorToHal(hal_cli, "wrong hal command");
    return;
  }

  // send to hal_cli either confirm or cancel
  if (fsm_input != FsmInput::kUserConfirm &&
      fsm_input != FsmInput::kUserCancel) {
    /*
     * TODO(kwstephenkim@google.com): change here when other user inputs must
     * be handled
     *
     */
    if (!packet::SendAck(hal_cli, session_id_, true,
                         "invalid user input error")) {
      // note that input is what we control in memory
      ConfUiCheck(false) << "Input must be either confirm or cancel for now.";
    }
    return;
  }

  ConfUiLog(DEBUG) << "In HandlieInSession, session" << session_id_
                   << "is sending the user input" << ToString(fsm_input);
  auto selection = UserResponse::kConfirm;
  if (fsm_input == FsmInput::kUserCancel) {
    selection = UserResponse::kCancel;
  }
  if (!packet::SendResponse(hal_cli, session_id_, selection)) {
    ConfUiLog(FATAL) << "I/O error in sending user response to HAL";
  }
  state_ = MainLoopState::kWaitStop;
  return;
}

void Session::HandleWaitStop(const bool is_user_input, SharedFD hal_cli,
                             const FsmInput fsm_input) {
  using namespace cuttlefish::confui::packet;

  if (is_user_input) {
    // ignore user input
    state_ = MainLoopState::kWaitStop;
    return;
  }
  if (fsm_input == FsmInput::kHalStop) {
    ConfUiLog(DEBUG) << "Handling Abort in kWaitStop.";
    Kill(hal_cli, "stopped");
    return;
  }
  ConfUiLog(FATAL) << "In WaitStop, received wrong HAL command "
                   << ToString(fsm_input);
  state_ = MainLoopState::kAwaitCleanup;
  return;
}

}  // end of namespace confui
}  // end of namespace cuttlefish

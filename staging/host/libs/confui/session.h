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

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include <teeui/msg_formatting.h>

#include "common/libs/confui/confui.h"
#include "host/libs/confui/cbor.h"
#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/confui/host_renderer.h"
#include "host/libs/confui/server_common.h"
#include "host/libs/confui/sign.h"

namespace cuttlefish {
namespace confui {

/**
 * Confirmation UI Session
 *
 * E.g. Two guest apps could drive confirmation UI respectively,
 * and both are alive at the moment. Each needs one session
 *
 */
class Session {
 public:
  Session(const std::string& session_name, const std::uint32_t display_num,
          ConfUiRenderer& host_renderer, HostModeCtrl& host_mode_ctrl,
          const std::string& locale = "en");

  bool IsConfUiActive() const;

  std::string GetId() { return session_id_; }

  MainLoopState GetState() { return state_; }

  MainLoopState Transition(SharedFD& hal_cli, const FsmInput fsm_input,
                           const ConfUiMessage& conf_ui_message);

  /**
   * this make a transition from kWaitStop or kInSession to kSuspend
   */
  bool Suspend(SharedFD hal_cli);

  /**
   * this make a transition from kRestore to the saved state
   */
  bool Restore(SharedFD hal_cli);

  // abort session
  void Abort();

  // client on the host wants to abort
  // should let the guest know it
  void UserAbort(SharedFD hal_cli);

  bool IsSuspended() const;
  void CleanUp();

  bool IsConfirm(const int x, const int y) {
    return renderer_.IsInConfirm(x, y);
  }

  bool IsCancel(const int x, const int y) { return renderer_.IsInCancel(x, y); }

  // tell if grace period has passed
  bool IsReadyForUserInput() const;

 private:
  bool IsUserInput(const FsmInput fsm_input) {
    return fsm_input == FsmInput::kUserEvent;
  }

  /** create a frame, and render it on the webRTC client
   *
   * note that this does not check host_ctrl_mode_
   */
  bool RenderDialog();

  // transition actions on each state per input
  // the new state will be save to the state_ at the end of each call
  //
  // when false is returned, the FSM must terminate
  // and, no need to let the guest know
  bool HandleInit(SharedFD hal_cli, const FsmInput fsm_input,
                  const ConfUiMessage& conf_ui_msg);

  bool HandleWaitStop(SharedFD hal_cli, const FsmInput fsm_input);

  bool HandleInSession(SharedFD hal_cli, const FsmInput fsm_input,
                       const ConfUiMessage& conf_ui_msg);

  // report with an error ack to HAL, and reset the FSM
  bool ReportErrorToHal(SharedFD hal_cli, const std::string& msg);

  void ScheduleToTerminate();

  const std::string session_id_;
  const std::uint32_t display_num_;
  ConfUiRenderer& renderer_;
  HostModeCtrl& host_mode_ctrl_;

  // only context to save
  std::string prompt_text_;
  std::string locale_;
  std::vector<teeui::UIOption> ui_options_;
  std::vector<std::uint8_t> extra_data_;
  // the second argument for resultCB of promptUserConfirmation
  std::vector<std::uint8_t> signed_confirmation_;
  std::vector<std::uint8_t> message_;

  std::unique_ptr<Cbor> cbor_;

  // effectively, this variables are shared with webRTC thread
  // the input demuxer will check the confirmation UI mode based on this
  std::atomic<MainLoopState> state_;
  MainLoopState saved_state_;  // for restore/suspend
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;
  std::unique_ptr<TimePoint> start_time_;
};
}  // end of namespace confui
}  // end of namespace cuttlefish

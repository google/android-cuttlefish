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

#include <memory>

#include "common/libs/confui/confui.h"
#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/confui/host_renderer.h"
#include "host/libs/confui/server_common.h"
#include "host/libs/confui/session.h"
#include "host/libs/screen_connector/screen_connector.h"

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
  Session(const std::string& session_id, const std::uint32_t display_num,
          ConfUiRenderer& host_renderer, HostModeCtrl& host_mode_ctrl,
          ScreenConnectorFrameRenderer& screen_connector,
          const std::string& locale = "en");

  bool IsConfUiActive() const;

  std::string GetId() { return session_id_; }

  MainLoopState GetState() { return state_; }

  MainLoopState Transition(const bool is_user_input, SharedFD& hal_cli,
                           const FsmInput fsm_input,
                           const std::string& additional_info);

  /**
   * this make a transition from kWaitStop or kInSession to kSuspend
   */
  bool Suspend(SharedFD hal_cli);

  /**
   * this make a transition from kRestore to the saved state
   */
  bool Restore(SharedFD hal_cli);

  // abort session
  bool Abort(SharedFD hal_cli);

  bool IsSuspended() const;
  void CleanUp();

 private:
  /** create a frame, and render it on the vnc/webRTC client
   *
   * note that this does not check host_ctrl_mode_
   */
  bool RenderDialog(const std::string& msg, const std::string& locale);

  // transition actions on each state per input
  // the new state will be save to the state_ at the end of each call
  void HandleInit(const bool is_user_input, SharedFD hal_cli,
                  const FsmInput fsm_input, const std::string& additional_info);

  void HandleWaitStop(const bool is_user_input, SharedFD hal_cli,
                      const FsmInput fsm_input);

  void HandleInSession(const bool is_user_input, SharedFD hal_cli,
                       const FsmInput fsm_input);

  bool Kill(SharedFD hal_cli, const std::string& response_msg);

  // report with an error ack to HAL, and reset the FSM
  void ReportErrorToHal(SharedFD hal_cli, const std::string& msg);

  const std::string session_id_;
  const std::uint32_t display_num_;
  // host renderer is shared across sessions
  ConfUiRenderer& renderer_;
  HostModeCtrl& host_mode_ctrl_;
  ScreenConnectorFrameRenderer& screen_connector_;

  // only context to save
  std::string prompt_;
  std::string locale_;

  // effectively, this variables are shared with vnc, webRTC thread
  // the input demuxer will check the confirmation UI mode based on this
  std::atomic<MainLoopState> state_;
  MainLoopState saved_state_;  // for restore/suspend
};
}  // end of namespace confui
}  // end of namespace cuttlefish

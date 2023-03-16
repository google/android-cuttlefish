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
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <teeui/utils.h>

#include "common/libs/concurrency/multiplexer.h"
#include "common/libs/concurrency/semaphore.h"
#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_fd.h"
#include "host/commands/kernel_log_monitor/utils.h"
#include "host/libs/config/logging.h"
#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/confui/host_renderer.h"
#include "host/libs/confui/host_virtual_input.h"
#include "host/libs/confui/server_common.h"
#include "host/libs/confui/session.h"

namespace cuttlefish {
namespace confui {
class HostServer : public HostVirtualInput {
 public:
  static HostServer& Get(HostModeCtrl& host_mode_ctrl,
                         ConfUiRenderer& host_renderer, SharedFD from_guest_fd,
                         SharedFD to_guest_fd);

  void Start();  // start this server itself
  virtual ~HostServer() {}

  // implement input interfaces. called by webRTC
  void TouchEvent(const int x, const int y, const bool is_down) override;
  void UserAbortEvent() override;
  bool IsConfUiActive() override;

 private:
  explicit HostServer(HostModeCtrl& host_mode_ctrl,
                      ConfUiRenderer& host_renderer, SharedFD from_guest_fd,
                      SharedFD to_guest_fd);
  HostServer() = delete;

  /**
   * basic prompt flow:
   * (1) Without preemption
   *  send "kStart" with confirmation message
   *  wait kCliAck from the host service with the echoed command
   *  wait the confirmation/cancellation (or perhaps reset?)
   *  send kStop
   *  wait kCliAck from the host service with the echoed command
   *
   * (2) With preemption (e.g.)
   *  send "kStart" with confirmation message
   *  wait kCliAck from the host service with the echoed command
   *  wait the confirmation/cancellation (or perhaps reset?)
   *  send kSuspend  // when HAL is preempted
   *  send kRestore  // when HAL resumes
   *  send kStop
   *
   *  From the host end, it is a close-to-Mealy FSM.
   *  There are four states S = {init, session, wait_ack, suspended}
   *
   *  'session' means in a confirmation session. 'wait_ack' means
   *  server sends the confirmation and waiting "stop" command from HAL
   *  'suspended' means the HAL service is preemptied. So, the host
   *  should render the Android guest frames but keep the confirmation
   *  UI session and frame
   *
   *  The inputs are I = {u, g}. 'u' is the user input from webRTC
   *  clients. Note that the host service serialized the concurrent user
   *  inputs from multiple clients. 'g' is the command from the HAL service
   *
   *  The transition rules:
   *    (S, I) --> (S, O) where O is the output
   *
   *   init, g(start) -->  session, set Conf UI mode, render a frame
   *   session, u(cancel/confirm) --> waitstop, send the result to HAL
   *   session, g(suspend) --> suspend, create a saved session
   *   session, g(abort)   --> init, clear saved frame
   *   waitstop, g(stop) --> init, clear saved frame
   *   waitstop, g(suspend) --> suspend, no need to save the session
   *   waitstop, g(abort) --> init, clear saved frame
   *   suspend, g(restore) --> return to the saved state, restore if there's a
   *                           saved session
   *   suspend, g(abort) --> init, clear saved frame
   *
   * For now, we did not yet implement suspend or abort.
   *
   */
  [[noreturn]] void MainLoop();
  void HalCmdFetcherLoop();

  bool IsVirtioConsoleOpen() const;
  // If !IsVirtioConsoleOpen(), LOG(FATAL) and return false
  bool CheckVirtioConsole();
  std::shared_ptr<Session> CreateSession(const std::string& session_name);
  void SendUserSelection(std::unique_ptr<ConfUiMessage>& input);

  void Transition(std::unique_ptr<ConfUiMessage>& input_ptr);
  std::string GetCurrentSessionId() {
    if (curr_session_) {
      return curr_session_->GetId();
    }
    return SESSION_ANY;
  }

  std::string GetCurrentState() {
    if (!curr_session_) {
      return {"kInvalid"};
    }
    return ToString(curr_session_->GetState());
  }

  const std::uint32_t display_num_;
  ConfUiRenderer& host_renderer_;
  HostModeCtrl& host_mode_ctrl_;

  std::shared_ptr<Session> curr_session_;

  SharedFD from_guest_fifo_fd_;
  SharedFD to_guest_fifo_fd_;

  using Multiplexer =
      Multiplexer<std::unique_ptr<ConfUiMessage>,
                  ThreadSafeQueue<std::unique_ptr<ConfUiMessage>>>;
  /*
   * Multiplexer has N queues. When pop(), it is going to sleep until
   * there's at least one item in at least one queue. The lower the Q
   * index is, the higher the priority is.
   *
   * For HostServer, we have a queue for the user input events, and
   * another for hal cmd/msg queues
   */
  Multiplexer input_multiplexer_;
  int hal_cmd_q_id_;         // Q id in input_multiplexer_
  int user_input_evt_q_id_;  // Q id in input_multiplexer_

  std::thread main_loop_thread_;
  std::thread hal_input_fetcher_thread_;
};

}  // end of namespace confui
}  // end of namespace cuttlefish

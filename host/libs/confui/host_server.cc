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

#include "host/libs/confui/host_server.h"

#include <functional>
#include <memory>
#include <optional>
#include <tuple>

#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_buf.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/confui/host_utils.h"
#include "host/libs/confui/secure_input.h"

namespace cuttlefish {
namespace confui {
namespace {

template <typename Derived, typename Base>
std::unique_ptr<Derived> DowncastTo(std::unique_ptr<Base>&& base) {
  Base* tmp = base.release();
  Derived* derived = static_cast<Derived*>(tmp);
  return std::unique_ptr<Derived>(derived);
}

}  // namespace

/**
 * null if not user/touch, or wrap it and ConfUiSecure{Selection,Touch}Message
 *
 * ConfUiMessage must NOT ConfUiSecure{Selection,Touch}Message types
 */
static std::unique_ptr<ConfUiMessage> WrapWithSecureFlag(
    std::unique_ptr<ConfUiMessage>&& base_msg, const bool secure) {
  switch (base_msg->GetType()) {
    case ConfUiCmd::kUserInputEvent: {
      auto as_selection =
          DowncastTo<ConfUiUserSelectionMessage>(std::move(base_msg));
      return ToSecureSelectionMessage(std::move(as_selection), secure);
    }
    case ConfUiCmd::kUserTouchEvent: {
      auto as_touch = DowncastTo<ConfUiUserTouchMessage>(std::move(base_msg));
      return ToSecureTouchMessage(std::move(as_touch), secure);
    }
    default:
      return nullptr;
  }
}

HostServer::HostServer(HostModeCtrl& host_mode_ctrl,
                       ConfUiRenderer& host_renderer,
                       const PipeConnectionPair& fd_pair)
    : display_num_(0),
      host_renderer_{host_renderer},
      host_mode_ctrl_(host_mode_ctrl),
      from_guest_fifo_fd_(fd_pair.from_guest_),
      to_guest_fifo_fd_(fd_pair.to_guest_) {
  const size_t max_elements = 20;
  auto ignore_new =
      [](ThreadSafeQueue<std::unique_ptr<ConfUiMessage>>::QueueImpl*) {
        // no op, so the queue is still full, and the new item will be discarded
        return;
      };
  hal_cmd_q_id_ = input_multiplexer_.RegisterQueue(
      HostServer::Multiplexer::CreateQueue(max_elements, ignore_new));
  user_input_evt_q_id_ = input_multiplexer_.RegisterQueue(
      HostServer::Multiplexer::CreateQueue(max_elements, ignore_new));
}

bool HostServer::IsVirtioConsoleOpen() const {
  return from_guest_fifo_fd_->IsOpen() && to_guest_fifo_fd_->IsOpen();
}

bool HostServer::CheckVirtioConsole() {
  if (IsVirtioConsoleOpen()) return true;
  ConfUiLog(FATAL) << "Virtio console is not open";
  return false;
}

void HostServer::Start() {
  if (!CheckVirtioConsole()) {
    return;
  }
  auto hal_cmd_fetching = [this]() { this->HalCmdFetcherLoop(); };
  auto main = [this]() { this->MainLoop(); };
  hal_input_fetcher_thread_ =
      thread::RunThread("HalInputLoop", hal_cmd_fetching);
  main_loop_thread_ = thread::RunThread("MainLoop", main);
  ConfUiLog(DEBUG) << "host service started.";
  return;
}

void HostServer::HalCmdFetcherLoop() {
  while (true) {
    if (!CheckVirtioConsole()) {
      return;
    }
    auto msg = RecvConfUiMsg(from_guest_fifo_fd_);
    if (!msg) {
      ConfUiLog(ERROR) << "Error in RecvConfUiMsg from HAL";
      // TODO(kwstephenkim): error handling
      // either file is not open, or ill-formatted message
      continue;
    }
    /*
     * In case of Vts test, the msg could be a user input. For now, we do not
     * enforce the input grace period for Vts. However, if ever we do, here is
     * where the time point check should happen. Once it is enqueued, it is not
     * always guaranteed to be picked up reasonably soon.
     */
    constexpr bool is_secure = false;
    auto to_override_if_user_input =
        WrapWithSecureFlag(std::move(msg), is_secure);
    if (to_override_if_user_input) {
      msg = std::move(to_override_if_user_input);
    }
    input_multiplexer_.Push(hal_cmd_q_id_, std::move(msg));
  }
}

/**
 * Send inputs generated not by auto-tester but by the human users
 *
 * Send such inputs into the command queue consumed by the state machine
 * in the main loop/current session.
 */
void HostServer::SendUserSelection(std::unique_ptr<ConfUiMessage>& input) {
  if (!curr_session_ ||
      curr_session_->GetState() != MainLoopState::kInSession ||
      !curr_session_->IsReadyForUserInput()) {
    // ignore
    return;
  }
  constexpr bool is_secure = true;
  auto secure_input = WrapWithSecureFlag(std::move(input), is_secure);
  input_multiplexer_.Push(user_input_evt_q_id_, std::move(secure_input));
}

void HostServer::TouchEvent(const int x, const int y, const bool is_down) {
  if (!is_down || !curr_session_) {
    return;
  }
  std::unique_ptr<ConfUiMessage> input =
      std::make_unique<ConfUiUserTouchMessage>(GetCurrentSessionId(), x, y);
  SendUserSelection(input);
}

void HostServer::UserAbortEvent() {
  if (!curr_session_) {
    return;
  }
  std::unique_ptr<ConfUiMessage> input =
      std::make_unique<ConfUiUserSelectionMessage>(GetCurrentSessionId(),
                                                   UserResponse::kUserAbort);
  SendUserSelection(input);
}

// read the comments in the header file
[[noreturn]] void HostServer::MainLoop() {
  while (true) {
    // this gets one input from either queue:
    // from HAL or from all webrtc clients
    // if no input, sleep until there is
    auto input_ptr = input_multiplexer_.Pop();
    auto& input = *input_ptr;
    const auto session_id = input.GetSessionId();
    const auto cmd = input.GetType();
    const std::string cmd_str(ToString(cmd));

    // take input for the Finite States Machine below
    std::string src = input.IsUserInput() ? "input" : "hal";
    ConfUiLog(VERBOSE) << "In Session " << GetCurrentSessionId() << ", "
                       << "in state " << GetCurrentState() << ", "
                       << "received input from " << src << " cmd =" << cmd_str
                       << " going to session " << session_id;

    if (!curr_session_) {
      if (cmd != ConfUiCmd::kStart) {
        ConfUiLog(VERBOSE) << ToString(cmd) << " to " << session_id
                           << " is ignored as there is no session to receive";
        continue;
      }
      // the session is created as kInit
      curr_session_ = CreateSession(input.GetSessionId());
    }
    if (cmd == ConfUiCmd::kUserTouchEvent) {
      ConfUiSecureUserTouchMessage& touch_event =
          static_cast<ConfUiSecureUserTouchMessage&>(input);
      auto [x, y] = touch_event.GetLocation();
      const bool is_confirm = curr_session_->IsConfirm(x, y);
      const bool is_cancel = curr_session_->IsCancel(x, y);
      ConfUiLog(INFO) << "Touch at [" << x << ", " << y << "] was "
                      << (is_cancel ? "CANCEL"
                                    : (is_confirm ? "CONFIRM" : "INVALID"));
      if (!is_confirm && !is_cancel) {
        // ignore, take the next input
        continue;
      }
      decltype(input_ptr) tmp_input_ptr =
          std::make_unique<ConfUiUserSelectionMessage>(
              GetCurrentSessionId(),
              (is_confirm ? UserResponse::kConfirm : UserResponse::kCancel));
      input_ptr =
          WrapWithSecureFlag(std::move(tmp_input_ptr), touch_event.IsSecure());
    }
    Transition(input_ptr);

    // finalize
    if (curr_session_ &&
        curr_session_->GetState() == MainLoopState::kAwaitCleanup) {
      curr_session_->CleanUp();
      curr_session_ = nullptr;
    }
  }  // end of the infinite while loop
}

std::shared_ptr<Session> HostServer::CreateSession(const std::string& name) {
  return std::make_shared<Session>(name, display_num_, host_renderer_,
                                   host_mode_ctrl_);
}

static bool IsUserAbort(ConfUiMessage& msg) {
  if (msg.GetType() != ConfUiCmd::kUserInputEvent) {
    return false;
  }
  ConfUiUserSelectionMessage& selection =
      static_cast<ConfUiUserSelectionMessage&>(msg);
  return (selection.GetResponse() == UserResponse::kUserAbort);
}

void HostServer::Transition(std::unique_ptr<ConfUiMessage>& input_ptr) {
  auto& input = *input_ptr;
  const auto session_id = input.GetSessionId();
  const auto cmd = input.GetType();
  const std::string cmd_str(ToString(cmd));
  FsmInput fsm_input = ToFsmInput(input);
  ConfUiLog(VERBOSE) << "Handling " << ToString(cmd);
  if (IsUserAbort(input)) {
    curr_session_->UserAbort(to_guest_fifo_fd_);
    return;
  }

  if (cmd == ConfUiCmd::kAbort) {
    curr_session_->Abort();
    return;
  }
  curr_session_->Transition(to_guest_fifo_fd_, fsm_input, input);
}

}  // end of namespace confui
}  // end of namespace cuttlefish

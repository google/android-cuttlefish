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

#include <chrono>
#include <functional>
#include <optional>
#include <tuple>

#include "common/libs/confui/confui.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/confui/host_utils.h"

namespace cuttlefish {
namespace confui {
static auto CuttlefishConfigDefaultInstance() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  CHECK(config) << "Config must not be null";
  return config->ForDefaultInstance();
}

static std::string HalGuestSocketPath() {
  return CuttlefishConfigDefaultInstance().confui_hal_guest_socket_path();
}

HostServer& HostServer::Get(
    HostModeCtrl& host_mode_ctrl,
    cuttlefish::ScreenConnectorFrameRenderer& screen_connector) {
  static HostServer host_server{host_mode_ctrl, screen_connector};
  return host_server;
}

HostServer::HostServer(
    cuttlefish::HostModeCtrl& host_mode_ctrl,
    cuttlefish::ScreenConnectorFrameRenderer& screen_connector)
    : display_num_(0),
      host_mode_ctrl_(host_mode_ctrl),
      screen_connector_{screen_connector},
      renderer_(display_num_),
      hal_socket_path_(HalGuestSocketPath()),
      input_multiplexer_{/* max n_elems */ 20, /* n_Qs */ 2} {
  hal_cmd_q_id_ = input_multiplexer_.GetNewQueueId();         // return 0
  user_input_evt_q_id_ = input_multiplexer_.GetNewQueueId();  // return 1
}

void HostServer::Start() {
  guest_hal_socket_ = cuttlefish::SharedFD::SocketLocalServer(
      hal_socket_path_, false, SOCK_STREAM, 0666);
  if (!guest_hal_socket_->IsOpen()) {
    ConfUiLog(FATAL) << "Confirmation UI host service mandates a server socket"
                     << "to which the guest HAL to connect.";
    return;
  }
  auto hal_cmd_fetching = [this]() { this->HalCmdFetcherLoop(); };
  auto main = [this]() { this->MainLoop(); };
  hal_input_fetcher_thread_ =
      thread::RunThread("HalInputLoop", hal_cmd_fetching);
  main_loop_thread_ = thread::RunThread("MainLoop", main);
  ConfUiLog(DEBUG) << "configured internal socket based input.";
  return;
}

void HostServer::HalCmdFetcherLoop() {
  hal_cli_socket_ = EstablishHalConnection();
  if (!hal_cli_socket_->IsOpen()) {
    ConfUiLog(FATAL)
        << "Confirmation UI host service mandates connection with HAL.";
    return;
  }
  while (true) {
    auto opted_msg = RecvConfUiMsg(hal_cli_socket_);
    if (!opted_msg) {
      ConfUiLog(ERROR) << "Error in RecvConfUiMsg from HAL";
      continue;
    }
    auto input = std::move(opted_msg.value());
    input_multiplexer_.Push(hal_cmd_q_id_, std::move(input));
  }
}

bool HostServer::SendUserSelection(UserResponse::type selection) {
  if (!curr_session_) {
    ConfUiLog(FATAL) << "Current session must not be null";
    return false;
  }
  if (curr_session_->GetState() != MainLoopState::kInSession) {
    // ignore
    return true;
  }

  std::lock_guard<std::mutex> lock(input_socket_mtx_);
  if (selection != UserResponse::kConfirm &&
      selection != UserResponse::kCancel) {
    ConfUiLog(FATAL) << selection << " must be either" << UserResponse::kConfirm
                     << "or" << UserResponse::kCancel;
    return false;  // not reaching here
  }

  ConfUiMessage input{GetCurrentSessionId(),
                      ToString(ConfUiCmd::kUserInputEvent), selection};

  input_multiplexer_.Push(user_input_evt_q_id_, std::move(input));
  return true;
}

void HostServer::PressConfirmButton(const bool is_down) {
  if (!is_down) {
    return;
  }
  // shared by N vnc/webRTC clients
  SendUserSelection(UserResponse::kConfirm);
}

void HostServer::PressCancelButton(const bool is_down) {
  if (!is_down) {
    return;
  }
  // shared by N vnc/webRTC clients
  SendUserSelection(UserResponse::kCancel);
}

bool HostServer::IsConfUiActive() {
  if (!curr_session_) {
    return false;
  }
  return curr_session_->IsConfUiActive();
}

SharedFD HostServer::EstablishHalConnection() {
  ConfUiLog(DEBUG) << "Waiting hal accepting";
  auto new_cli = SharedFD::Accept(*guest_hal_socket_);
  ConfUiLog(DEBUG) << "hal client accepted";
  return new_cli;
}

std::unique_ptr<Session> HostServer::ComputeCurrentSession(
    const std::string& session_id) {
  if (curr_session_ && (GetCurrentSessionId() != session_id)) {
    ConfUiLog(FATAL) << curr_session_->GetId() << " is active and in the"
                     << GetCurrentState() << "but HAL sends command to"
                     << session_id;
  }
  if (curr_session_) {
    return std::move(curr_session_);
  }

  // pick up a new session, or create one
  auto result = GetSession(session_id);
  if (result) {
    return std::move(result);
  }

  auto raw_ptr = new Session(session_id, display_num_, renderer_,
                             host_mode_ctrl_, screen_connector_);
  result = std::unique_ptr<Session>(raw_ptr);
  // note that the new session is directly going to curr_session_
  // when it is suspended, it will be moved to session_map_
  return std::move(result);
}

// read the comments in the header file
[[noreturn]] void HostServer::MainLoop() {
  while (true) {
    // this gets one input from either queue:
    // from HAL or from all webrtc/vnc clients
    // if no input, sleep until there is
    const auto input = input_multiplexer_.Pop();
    const auto& [session_id, cmd_str, additional_info] = input;

    // take input for the Finite States Machine below
    const ConfUiCmd cmd = ToCmd(cmd_str);
    const bool is_user_input = (cmd == ConfUiCmd::kUserInputEvent);
    std::string src = is_user_input ? "input" : "hal";

    ConfUiLog(DEBUG) << "In Session" << GetCurrentSessionId() << ","
                     << "in state" << GetCurrentState() << ","
                     << "received input from" << src << "cmd =" << cmd_str
                     << "and additional_info =" << additional_info
                     << "going to session" << session_id;

    FsmInput fsm_input = ToFsmInput(input);

    if (is_user_input && !curr_session_) {
      // discard the input, there's no session to take it yet
      // actually, no confirmation UI screen is available
      ConfUiLog(DEBUG) << "Took user input but no active session is available.";
      continue;
    }

    /**
     * if the curr_session_ is null, create one
     * if the curr_session_ is not null but the session id doesn't match,
     * something is wrong. Confirmation UI doesn't allow preemption by
     * another confirmation UI session back to back. When it's preempted,
     * HAL must send "kSuspend"
     *
     */
    curr_session_ = ComputeCurrentSession(session_id);
    ConfUiLog(DEBUG) << "Host service picked up "
                     << (curr_session_ ? curr_session_->GetId()
                                       : "null session");
    ConfUiLog(DEBUG) << "The state of current session is "
                     << (curr_session_ ? ToString(curr_session_->GetState())
                                       : "null session");

    if (is_user_input) {
      curr_session_->Transition(is_user_input, hal_cli_socket_, fsm_input,
                                additional_info);
    } else {
      ConfUiCmd cmd = ToCmd(cmd_str);
      switch (cmd) {
        case ConfUiCmd::kSuspend:
          curr_session_->Suspend(hal_cli_socket_);
          break;
        case ConfUiCmd::kRestore:
          curr_session_->Restore(hal_cli_socket_);
          break;
        case ConfUiCmd::kAbort:
          curr_session_->Abort(hal_cli_socket_);
          break;
        default:
          curr_session_->Transition(is_user_input, hal_cli_socket_, fsm_input,
                                    additional_info);
          break;
      }
    }

    // check the session if it is inactive (e.g. init, suspended)
    // and if it is done (transitioned to init from any other state)
    if (curr_session_->IsSuspended()) {
      session_map_[GetCurrentSessionId()] = std::move(curr_session_);
      curr_session_ = nullptr;
      continue;
    }

    if (curr_session_->GetState() == MainLoopState::kAwaitCleanup) {
      curr_session_->CleanUp();
      curr_session_ = nullptr;
    }
    // otherwise, continue with keeping the curr_session_
  }  // end of the infinite while loop
}

}  // end of namespace confui
}  // end of namespace cuttlefish

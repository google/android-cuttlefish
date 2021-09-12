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
#include "common/libs/fs/shared_buf.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/confui/host_utils.h"

namespace cuttlefish {
namespace confui {
static auto CuttlefishConfigDefaultInstance() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  CHECK(config) << "Config must not be null";
  return config->ForDefaultInstance();
}

static int HalHostVsockPort() {
  return CuttlefishConfigDefaultInstance().confui_host_vsock_port();
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
      hal_vsock_port_(HalHostVsockPort()) {
  ConfUiLog(DEBUG) << "Confirmation UI Host session is listening on: "
                   << hal_vsock_port_;
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

void HostServer::Start() {
  guest_hal_socket_ =
      cuttlefish::SharedFD::VsockServer(hal_vsock_port_, SOCK_STREAM);
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
  ConfUiLog(DEBUG) << "configured internal vsock based input.";
  return;
}

void HostServer::HalCmdFetcherLoop() {
  while (true) {
    if (!hal_cli_socket_->IsOpen()) {
      ConfUiLog(DEBUG) << "client is disconnected";
      std::unique_lock<std::mutex> lk(socket_flag_mtx_);
      hal_cli_socket_ = EstablishHalConnection();
      is_socket_ok_ = true;
      continue;
    }
    auto msg = RecvConfUiMsg(hal_cli_socket_);
    if (!msg) {
      ConfUiLog(ERROR) << "Error in RecvConfUiMsg from HAL";
      hal_cli_socket_->Close();
      is_socket_ok_ = false;
      continue;
    }
    input_multiplexer_.Push(hal_cmd_q_id_, std::move(msg));
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
      selection != UserResponse::kCancel &&
      selection != UserResponse::kUserAbort) {
    ConfUiLog(FATAL) << selection << " must be either "
                     << UserResponse::kConfirm << " or "
                     << UserResponse::kCancel << " or "
                     << UserResponse::kUserAbort;
    return false;  // not reaching here
  }

  auto input = CreateFromUserSelection(GetCurrentSessionId(), selection);

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

void HostServer::UserAbortEvent() {
  // shared by N vnc/webRTC clients
  SendUserSelection(UserResponse::kUserAbort);
}

bool HostServer::IsConfUiActive() {
  if (!curr_session_) {
    return false;
  }
  return curr_session_->IsConfUiActive();
}

SharedFD HostServer::EstablishHalConnection() {
  using namespace std::chrono_literals;
  while (true) {
    ConfUiLog(VERBOSE) << "Waiting hal accepting";
    auto new_cli = SharedFD::Accept(*guest_hal_socket_);
    ConfUiLog(VERBOSE) << "hal client accepted";
    if (new_cli->IsOpen()) {
      return new_cli;
    }
    std::this_thread::sleep_for(500ms);
  }
}

// read the comments in the header file
[[noreturn]] void HostServer::MainLoop() {
  while (true) {
    // this gets one input from either queue:
    // from HAL or from all webrtc/vnc clients
    // if no input, sleep until there is
    auto input_ptr = input_multiplexer_.Pop();
    auto& input = *input_ptr;
    const auto session_id = input.GetSessionId();
    const auto cmd = input.GetType();
    const std::string cmd_str(ToString(cmd));

    // take input for the Finite States Machine below
    const bool is_user_input = (cmd == ConfUiCmd::kUserInputEvent);
    std::string src = is_user_input ? "input" : "hal";
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
    Transition(input_ptr);

    // finalize
    if (curr_session_ &&
        curr_session_->GetState() == MainLoopState::kAwaitCleanup) {
      curr_session_->CleanUp();
      curr_session_ = nullptr;
    }
  }  // end of the infinite while loop
}

std::unique_ptr<Session> HostServer::CreateSession(const std::string& name) {
  return std::make_unique<Session>(name, display_num_, renderer_,
                                   host_mode_ctrl_, screen_connector_);
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
  const bool is_user_input = (cmd == ConfUiCmd::kUserInputEvent);
  FsmInput fsm_input = ToFsmInput(input);
  ConfUiLog(VERBOSE) << "Handling " << ToString(cmd);
  if (IsUserAbort(input)) {
    curr_session_->UserAbort(hal_cli_socket_);
    return;
  }

  if (cmd == ConfUiCmd::kAbort) {
    curr_session_->Abort();
    return;
  }
  curr_session_->Transition(is_user_input, hal_cli_socket_, fsm_input, input);
}

}  // end of namespace confui
}  // end of namespace cuttlefish

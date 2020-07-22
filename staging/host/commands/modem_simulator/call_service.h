//
// Copyright (C) 2020 The Android Open Source Project
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

#include "host/commands/modem_simulator/modem_service.h"
#include "host/commands/modem_simulator/network_service.h"
#include "host/commands/modem_simulator/sim_service.h"

namespace cuttlefish {

class CallService : public ModemService, public std::enable_shared_from_this<CallService>  {
 public:
  CallService(int32_t service_id, ChannelMonitor* channel_monitor,
              ThreadLooper* thread_looper);
  ~CallService() = default;

  CallService(const CallService &) = delete;
  CallService &operator=(const CallService &) = delete;

  void SetupDependency(SimService* sim, NetworkService* net);

  void HandleDial(const Client& client, const std::string& command);
  void HandleAcceptCall(const Client& client);
  void HandleRejectCall(const Client& client);
  void HandleCurrentCalls(const Client& client);
  void HandleHangup(const Client& client, const std::string& command);
  void HandleMute(const Client& client, const std::string& command);
  void HandleSendDtmf(const Client& client, const std::string& command);
  void HandleCancelUssd(const Client& client, const std::string& command);
  void HandleEmergencyMode(const Client& client, const std::string& command);
  void HandleRemoteCall(const Client& client, const std::string& command);

 private:
  void InitializeServiceState();
  std::vector<CommandHandler> InitializeCommandHandlers();
  void SimulatePendingCallsAnswered();
  void CallStateUpdate();

  struct CallStatus {
    enum CallState {
      CALL_STATE_ACTIVE = 0,
      CALL_STATE_HELD,
      CALL_STATE_DIALING,
      CALL_STATE_ALERTING,
      CALL_STATE_INCOMING,
      CALL_STATE_WAITING,
      CALL_STATE_HANGUP
    };

    // ctors
    CallStatus()
      : call_state(CALL_STATE_ACTIVE),
        is_mobile_terminated(true),
        is_international(false),
        is_voice_mode(true),
        is_multi_party(false),
        can_present_number(true) {}

    CallStatus(const std::string_view number)
        : call_state(CALL_STATE_INCOMING),
          is_mobile_terminated(true),
          is_international(false),
          is_voice_mode(true),
          is_multi_party(false),
          number(number),
          can_present_number(true) {}

    bool isCallBackground() {
      return call_state == CALL_STATE_HELD;
    }

    bool isCallActive() {
      return call_state == CALL_STATE_ACTIVE;
    }

    bool isCallDialing() {
      return call_state == CALL_STATE_DIALING;
    }

    bool isCallIncoming() {
      return call_state == CALL_STATE_INCOMING;
    }

    bool isCallWaiting() {
      return call_state == CALL_STATE_WAITING;
    }

    bool isCallAlerting() {
      return call_state == CALL_STATE_ALERTING;
    }

    bool SetCallBackground() {
      if (call_state == CALL_STATE_ACTIVE) {
        call_state = CALL_STATE_HELD;
        return true;
      }

      return false;
    }

    bool SetCallActive() {
      if (call_state == CALL_STATE_INCOMING || call_state == CALL_STATE_WAITING ||
          call_state == CALL_STATE_DIALING || call_state == CALL_STATE_HELD) {
        call_state = CALL_STATE_ACTIVE;
        return true;
      }

      return false;
    }

    // date member public
    CallState call_state;
    bool is_mobile_terminated;
    bool is_international;
    bool is_voice_mode;
    bool is_multi_party;
    bool is_remote_call;
    std::optional<cuttlefish::SharedFD> remote_client;
    std::optional<int32_t> timeout_serial;
    std::string number;
    bool can_present_number;
  };
  using CallToken = std::pair<int, std::string>;

  void SendCallStatusToRemote(CallStatus& call, CallStatus::CallState state);
  void TimerWaitingRemoteCallResponse(CallToken token);

  // private data members
  SimService* sim_service_;
  NetworkService* network_service_;
  int32_t last_active_call_index_;
  std::map<int, CallStatus> active_calls_;
  bool in_emergency_mode_;
  bool mute_on_;
};

}  // namespace

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

#include "host/libs/config/cuttlefish_config.h"

#include "call_service.h"
#include "nvram_config.h"

namespace cuttlefish {

CallService::CallService(int32_t service_id, ChannelMonitor* channel_monitor,
                         ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper) {
  InitializeServiceState();
}

void CallService::InitializeServiceState() {
  auto nvram_config = NvramConfig::Get();
  auto instance = nvram_config->ForInstance(service_id_);
  in_emergency_mode_ = instance.emergency_mode();

  last_active_call_index_ = 1;
  mute_on_ = false;
}

void CallService::SetupDependency(SimService* sim, NetworkService* net) {
  sim_service_ = sim;
  network_service_ = net;
}

std::vector<CommandHandler> CallService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      CommandHandler("D",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleDial(client, cmd);
                     }),
      CommandHandler(
          "A",
          [this](const Client& client) { this->HandleAcceptCall(client); }),
      CommandHandler(
          "H",
          [this](const Client& client) { this->HandleRejectCall(client); }),
      CommandHandler(
          "+CLCC",
          [this](const Client& client) { this->HandleCurrentCalls(client); }),
      CommandHandler("+CHLD=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleHangup(client, cmd);
                     }),
      CommandHandler("+CMUT=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleMute(client, cmd);
                     }),
      CommandHandler("+VTS=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSendDtmf(client, cmd);
                     }),
      CommandHandler("+CUSD=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleCancelUssd(client, cmd);
                     }),
      CommandHandler("+WSOS=0",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleEmergencyMode(client, cmd);
                     }),
      CommandHandler("+REMOTECALL",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleRemoteCall(client, cmd);
                     }),
  };
  return (command_handlers);
}

// This also resumes held calls
void CallService::SimulatePendingCallsAnswered() {
  for (auto& iter : active_calls_) {
    if (iter.second.isCallDialing()) {
      iter.second.SetCallActive();
    }
  }
}

void CallService::TimerWaitingRemoteCallResponse(CallToken call_token) {
  LOG(DEBUG) << "Dialing id: " << call_token.first
             << ", number: " << call_token.second << "timeout, cancel";
  auto iter = active_calls_.find(call_token.first);
  if (iter != active_calls_.end() && iter->second.number == call_token.second) {
    if (iter->second.remote_client != std::nullopt) {
      CloseRemoteConnection(*(iter->second.remote_client));
    }
    active_calls_.erase(iter);  // match
    CallStateUpdate();
  }  // else not match, ignore
}

/* ATD */
void CallService::HandleDial(const Client& client, const std::string& command) {
  // Check the network registration state
  auto registration_state = NetworkService::NET_REGISTRATION_UNKNOWN;
  if (network_service_) {
    registration_state = network_service_->GetVoiceRegistrationState();
  }

  bool emergency_only = false;
  if (registration_state == NetworkService::NET_REGISTRATION_HOME ||
      registration_state == NetworkService::NET_REGISTRATION_ROAMING) {
    emergency_only = false;
  } else if (registration_state == NetworkService::NET_REGISTRATION_EMERGENCY) {
    emergency_only = true;
  } else {
    client.SendCommandResponse(kCmeErrorNoNetworkService);
    return;
  }

  CommandParser cmd(command);
  cmd.SkipPrefixAT();

  std::string number;
  bool emergency_number = false;
  /**
   * Normal dial:     ATDnumber[clir];
   * Emergency dial:  ATDnumber@[category],#[clir];
   */
  auto pos = cmd->find_last_of('@');
  if (pos != std::string_view::npos) {
    emergency_number = true;
    number = cmd->substr(1, pos -1); // Skip 'D' and ignore category, clir
  } else {  // Remove 'i' or 'I' or ';'
    pos = cmd->find_last_of('i');
    if (pos == std::string_view::npos) {
      pos = cmd->find_last_of('I');
      if (pos == std::string_view::npos) {
        pos = cmd->find_last_of(';');
      }
    }
    if (pos == std::string_view::npos) {
      number = cmd->substr(1);
    } else {
      number = cmd->substr(1, pos -1);
    }
  }

  // Check the number is valid digits or not
  if (strspn(number.c_str(), "1234567890") != number.size()) {
    client.SendCommandResponse(kCmeErrorInCorrectParameters);
    return;
  }

  if (emergency_only && !emergency_number) {
    client.SendCommandResponse(kCmeErrorNetworkNotAllowedEmergencyCallsOnly);
    return;
  }

  // If the number is not emergency number, FDN enabled and the number is not in
  // the fdn list, return kCmeErrorFixedDialNumberOnlyAllowed.
  if (!emergency_number && sim_service_->IsFDNEnabled() &&
      !sim_service_->IsFixedDialNumber(number)) {
    client.SendCommandResponse(kCmeErrorFixedDialNumberOnlyAllowed);
    return;
  }

  int port = 0;
  if (number.length() == 11) {
    port = std::stoi(number.substr(7));
  } else if (number.length() == 4) {
    port = std::stoi(number);
  }

  if (port >= kRemotePortRange.first &&
      port <= kRemotePortRange.second) {  // May be a remote call
    std::stringstream ss;
    ss << port;
    auto remote_port = ss.str();
    auto remote_client = ConnectToRemoteCvd(remote_port);
    if (!remote_client->IsOpen()) {
      client.SendCommandResponse(kCmeErrorNoNetworkService);
      return;
    }
    auto local_host_port = GetHostPort();
    if (local_host_port == remote_port) {
      client.SendCommandResponse(kCmeErrorOperationNotAllowed);
      return;
    }

    if (channel_monitor_) {
      channel_monitor_->SetRemoteClient(remote_client, false);
    }

    ss.clear();
    ss.str("");
    ss << "AT+REMOTECALL=4,0,0,\"" << local_host_port << "\",129";

    SendCommandToRemote(remote_client, "REM0");
    SendCommandToRemote(remote_client, ss.str());

    CallStatus call_status(remote_port);
    call_status.is_remote_call = true;
    call_status.is_mobile_terminated = false;
    call_status.call_state = CallStatus::CALL_STATE_DIALING;
    call_status.remote_client = remote_client;
    int index = last_active_call_index_++;

    auto call_token = std::make_pair(index, call_status.number);
    call_status.timeout_serial = thread_looper_->PostWithDelay(
        std::chrono::minutes(1),
        makeSafeCallback<CallService>(
            weak_from_this(), [call_token](CallService* me) {
              me->TimerWaitingRemoteCallResponse(call_token);
            }));

    active_calls_[index] = call_status;
  } else {
    CallStatus call_status(number);
    call_status.is_mobile_terminated = false;
    call_status.call_state = CallStatus::CALL_STATE_DIALING;
    auto index = last_active_call_index_++;
    active_calls_[index] = call_status;

    if (emergency_number) {
      in_emergency_mode_ = true;
      SendUnsolicitedCommand("+WSOS: 1");
    }
    thread_looper_->PostWithDelay(std::chrono::seconds(1),
        makeSafeCallback(this, &CallService::SimulatePendingCallsAnswered));
  }

  client.SendCommandResponse("OK");
  sleep(2);
}

void CallService::SendCallStatusToRemote(CallStatus& call,
                                         CallStatus::CallState state) {
  if (call.is_remote_call && call.remote_client != std::nullopt) {
    std::stringstream ss;
    ss << "AT+REMOTECALL=" << state << ","
                           << call.is_voice_mode << ","
                           << call.is_multi_party << ",\""
                           << GetHostPort() << "\","
                           << call.is_international;

    SendCommandToRemote(*(call.remote_client), ss.str());
    if (state == CallStatus::CALL_STATE_HANGUP) {
      CloseRemoteConnection(*(call.remote_client));
    }
  }
}

/* ATA */
void CallService::HandleAcceptCall(const Client& client) {
  for (auto& iter : active_calls_) {
    if (iter.second.isCallIncoming()) {
      iter.second.SetCallActive();
      SendCallStatusToRemote(iter.second, CallStatus::CALL_STATE_ACTIVE);
    } else if (iter.second.isCallActive()) {
      iter.second.SetCallBackground();
      SendCallStatusToRemote(iter.second, CallStatus::CALL_STATE_HELD);
    }
  }

  client.SendCommandResponse("OK");
}

/* ATH */
void CallService::HandleRejectCall(const Client& client) {
  for (auto iter = active_calls_.begin(); iter != active_calls_.end();) {
    /* ATH: hangup, since user is busy */
    if (iter->second.isCallIncoming()) {
      SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_HANGUP);
      iter = active_calls_.erase(iter);
    }
    ++iter;
  }

  client.SendCommandResponse("OK");
}

/**
 * AT+CLCC
 *   Returns list of current calls of MT. If command succeeds but no
 *   calls are available, no information response is sent to TE.
 *
 *   command             Possible response(s)
 *   AT+CLCC               [+CLCC: <ccid1>,<dir>,<stat>,<mode>,<mpty>
 *                         [,<number>,<type>[,<alpha>[,<priority>
 *                         [,<CLI validity>]]]][<CR><LF>
 *                         +CLCC: <ccid2>,<dir>,<stat>,<mode>,<mpty>
 *                         [,<number>,<type>[,<alpha>[,<priority>[,<CLI validity>]]]]
 *                         +CME ERROR: <err>
 *
 * <ccidx>: integer type. This number can be used in +CHLD command
 * operations. Value range is from 1 to N. N, the maximum number of
 * simultaneous call control processes is implementation specific.
 * <dir>: integer type
 *       0 mobile originated (MO) call
         1 mobile terminated (MT) call
 * <stat>: integer type (state of the call)
 *       0 active
 *       1 held
 *       2 dialing (MO call)
 *       3 alerting (MO call)
 *       4 incoming (MT call)
 *       5 waiting (MT call)
 * <mode>: integer type (bearer/teleservice)
 *       0 voice
 *       1 data
 *       2 fax
 *       3 voice followed by data, voice mode
 *       4 alternating voice/data, voice mode
 *       5 alternating voice/fax, voice mode
 *       6 voice followed by data, data mode
 *       7 alternating voice/data, data mode
 *       8 alternating voice/fax, fax mode
 *       9 unknown
 * <mpty>: integer type
 *       0 call is not one of multiparty (conference) call parties
 *       1 call is one of multiparty (conference) call parties
 * <number>: string type phone number in format specified by <type>.
 * <type>: type of address octet in integer format
 *
 *see RIL_REQUEST_GET_CURRENT_CALLS in RIL
 */
void CallService::HandleCurrentCalls(const Client& client) {
  std::vector<std::string> responses;
  std::stringstream ss;

  // AT+CLCC
  // [+CLCC: <ccid1>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>[,<alpha>[,<priority>[,<CLI validity>]]]]
  // [+CLCC: <ccid2>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>[,<alpha>[,<priority>[,<CLI validity>]]]]
  // [...]]]
  for (auto iter = active_calls_.begin(); iter != active_calls_.end(); ++iter) {
    int index = iter->first;
    int dir = iter->second.is_mobile_terminated;
    CallStatus::CallState call_state = iter->second.call_state;
    int mode = iter->second.is_voice_mode;
    int mpty = iter->second.is_multi_party;
    int type = iter->second.is_international ? 145 : 129;
    std::string number = iter->second.number;

    ss.clear();
    ss << "+CLCC: " << index << "," << dir << "," << call_state << ","
        << mode << "," << mpty << "," << number<<  "," << type;
    responses.push_back(ss.str());
    ss.str("");
  }

  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CHLD
 *   This command allows the control of the following call related services:
 *   1) a call can be temporarily disconnected from the MT but the connection
 *      is retained by the network;
 *   2) multiparty conversation (conference calls);
 *   3) the served subscriber who has two calls (one held and the other
 *     either active or alerting) can connect the other parties and release
 *     the served subscriber's own connection.
 *
 *   Calls can be put on hold, recovered, released, added to conversation,
 *   and transferred similarly.
 *
 *   command             Possible response(s)
 *   +CHLD=<n>           +CME ERROR: <err>
 *
 *   +CHLD=?             +CHLD: (list of supported <n>s)
 *   e.g. +CHLD: (0,1,1x,2,2x,3,4)
 *
 *
 * see RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND
 *     RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND
 *     RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE
 *     RIL_REQUEST_CONFERENCE
 *     RIL_REQUEST_SEPARATE_CONNECTION
 *     RIL_REQUEST_HANGUP
 *     RIL_REQUEST_UDUB in RIL
 */
void CallService::HandleHangup(const Client& client,
                               const std::string& command) {
  std::vector<std::string> responses;
  CommandParser cmd(command);
  cmd.SkipPrefix();

  std::string action(*cmd);
  int n = std::stoi(action.substr(0, 1));
  int index = -1;
  if (cmd->length() > 1) {
    index = std::stoi(action.substr(1));
  }

  switch (n) {
    case 0:  // Release all held calls or set User Determined User Busy(UDUB) for a waiting call
      for (auto iter = active_calls_.begin(); iter != active_calls_.end();) {
        if (iter->second.isCallIncoming() ||
            iter->second.isCallBackground() ||
            iter->second.isCallWaiting()) {
          SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_HANGUP);
          iter = active_calls_.erase(iter);
        } else {
          ++iter;
        }
      }
      break;
    case 1:
      if (index == -1) {  // Release all active calls and accepts the other(hold or waiting) call
        for (auto iter = active_calls_.begin(); iter != active_calls_.end();) {
          if (iter->second.isCallActive()) {
            SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_HANGUP);
            iter = active_calls_.erase(iter);
            continue;
          } else if (iter->second.isCallBackground() ||
              iter->second.isCallWaiting()) {
            iter->second.SetCallActive();
            SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_ACTIVE);
          }
          ++iter;
        }
      } else {  // Release a specific active call
        auto iter = active_calls_.find(index);
        if (iter != active_calls_.end()) {
          SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_HANGUP);
          active_calls_.erase(iter);
        }
      }
      break;
    case 2:
      if (index == -1) {  // Place all active calls and the waiting calls, activates all held calls
        for (auto& iter : active_calls_) {
          if (iter.second.isCallActive() || iter.second.isCallWaiting()) {
            iter.second.SetCallBackground();
            SendCallStatusToRemote(iter.second, CallStatus::CALL_STATE_HELD);
          } else if (iter.second.isCallBackground()) {
            iter.second.SetCallActive();
            SendCallStatusToRemote(iter.second, CallStatus::CALL_STATE_ACTIVE);
          }
        }
      } else {  // Disconnect a call from the conversation
        auto iter = active_calls_.find(index);
        if (iter != active_calls_.end()) {
          SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_HANGUP);
          active_calls_.erase(iter);
        }
      }
      break;
    case 3:  // Adds an held call to the conversation
      for (auto iter = active_calls_.begin(); iter != active_calls_.end(); ++iter) {
        if (iter->second.isCallBackground()) {
          iter->second.SetCallActive();
          SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_ACTIVE);
        }
      }
      break;
    case 4:  // Connect the two calls
      for (auto iter = active_calls_.begin(); iter != active_calls_.end(); ++iter) {
        if (iter->second.isCallBackground()) {
          iter->second.SetCallActive();
          SendCallStatusToRemote(iter->second, CallStatus::CALL_STATE_ACTIVE);
        }
      }
      break;
    default:
      client.SendCommandResponse(kCmeErrorOperationNotAllowed);
      return;
  }
  client.SendCommandResponse("OK");
}

/**
 * AT+CMUT
 *   This command is used to enable and disable the uplink voice muting
 * during a voice call.
 *   Read command returns the current value of <n>.
 *
 * Command          Possible response(s)
 * +CMUT=[<n>]        +CME ERROR: <err>
 * +CMUT?             +CMUT: <n>
 *                    +CME ERROR: <err>
 *
 * <n>: integer type
 *   0 mute off
 *   1 mute on
 *
 * see RIL_REQUEST_SET_MUTE or RIL_REQUEST_GET_MUTE in RIL
 */
void CallService::HandleMute(const Client& client, const std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;

  CommandParser cmd(command);
  cmd.SkipPrefix();  // If AT+CMUT?, it remains AT+CMUT?

  if (cmd == "AT+CMUT?") {
    ss << "+CMUT: " << mute_on_;
    responses.push_back(ss.str());
  } else {  // AT+CMUT = <n>
    int n = cmd.GetNextInt();
    switch (n) {
      case 0:  // Mute off
        mute_on_ = false;
        break;
      case 1:  // Mute on
        mute_on_ = true;
        break;
      default:
        client.SendCommandResponse(kCmeErrorInCorrectParameters);
        return;
    }
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+VTS
 *   This command transmits DTMF, after a successful call connection.
 * Setting Command is used to send one or more ASCII characters which make
 * MSC (Mobile Switching Center) send DTMF tone to remote User.
 *
 * Command                         Possible response(s)
 * AT+VTS=<dtmf>[,<duration>]        +CME ERROR: <err>
 *
 * <dtmf>
 *   A single ASCII character in the set { 0 -9, #, *, A – D}.
 * <duration>
 *   Refer to duration value range of +VTD command
 *
 * see RIL_REQUEST_DTMF in RIL
 */
void CallService::HandleSendDtmf(const Client& client,
                                 const std::string& /*command*/) {
  client.SendCommandResponse("OK");
}

void CallService::HandleCancelUssd(const Client& client,
                                   const std::string& /*command*/) {
  client.SendCommandResponse("OK");
}

/**
 * AT+WSOS
 *
 * Command          Possible response(s)
 * +WSOS=[<n>]        +CME ERROR: <err>
 * +WSOS?             +WSOS: <n>
 *                    +CME ERROR: <err>
 *
 * <n>: integer type
 *   0 enter emergency mode
 *   1 exit emergency mode
 *
 * see RIL_REQUEST_EXIT_EMERGENCY_CALLBACK_MODE
 *     RIL_UNSOL_ENTER_EMERGENCY_CALLBACK_MODE
 *     RIL_UNSOL_EXIT_EMERGENCY_CALLBACK_MODE in RIL
 */
void CallService::HandleEmergencyMode(const Client& client,
                                      const std::string& command) {
  std::vector<std::string> responses;
  CommandParser cmd(command);
  cmd.SkipPrefix();

  if (cmd == "AT+WSOS?") {
    std::stringstream ss;
    ss << "+WSOS: " << in_emergency_mode_;
    responses.push_back(ss.str());
  } else {
    int n = cmd.GetNextInt();
    switch (n) {
      case 0:  // Exit
        in_emergency_mode_ = false;
        break;
      case 1:  // Enter
        in_emergency_mode_ = true;
        break;
      default:
        client.SendCommandResponse(kCmeErrorInCorrectParameters);
        return;
    }
    auto nvram_config = NvramConfig::Get();
    auto instance = nvram_config->ForInstance(service_id_);
    instance.set_emergency_mode(in_emergency_mode_);
    NvramConfig::SaveToFile();
  }
  client.SendCommandResponse("OK");
}

void CallService::CallStateUpdate() {
  SendUnsolicitedCommand("RING");
}

/**
 * AT+REMOTECALL=<dir>,<stat>,<mode>,<mpty>,<number>,<num_type>
 *   This command allows to dial a remote voice call with another cuttlefish
 * emulator. If request is successful, the remote emulator can simulate hold on,
 * hang up, reject and so on.
 *
 * e.g. AT+REMOTECALL=4,0,0,6521,129
 *
 * <stat>: integer type (state of the call)
 *       0 active
 *       1 held
 *       2 dialing (MO call)
 *       3 alerting (MO call)
 *       4 incoming (MT call)
 *       5 waiting (MT call)
 * <mode>: integer type
 *       0 voice
 *       1 data
 *       2 fax
 *       3 voice followed by data, voice mode
 *       4 alternating voice/data, voice mode
 *       5 alternating voice/fax, voice mode
 *       6 voice followed by data, data mode
 *       7 alternating voice/data, data mode
 *       8 alternating voice/fax, fax mode
 *       9 unknown
 * <mpty>: integer type
 *       0 call is not one of multiparty (conference) call parties
 *       1 call is one of multiparty (conference) call parties
 * <number>: string here maybe remote port
 * <num_type>: type of address octet in integer format
 *
 * Note: reason should be added to indicate why hang up. Since not realizing
 *       RIL_LAST_CALL_FAIL_CAUSE, delay to be implemented.
 */
void CallService::HandleRemoteCall(const Client& client,
                                   const std::string& command) {
  CommandParser cmd(command);
  cmd.SkipPrefix();

  int state = cmd.GetNextInt();
  int mode = cmd.GetNextInt();
  int mpty = cmd.GetNextInt();
  auto number = cmd.GetNextStr();
  int num_type = cmd.GetNextInt();

  // According to the number to determine whether it is a existing call
  auto iter = active_calls_.begin();
  for (; iter != active_calls_.end(); ++iter) {
    if (iter->second.number == number) {
      break;
    }
  }

  switch (state) {
    case CallStatus::CALL_STATE_ACTIVE: {
      if (iter != active_calls_.end()) {
        iter->second.SetCallActive();
        if (iter->second.timeout_serial != std::nullopt) {
          thread_looper_->CancelSerial(*(iter->second.timeout_serial));
        }
      }
      break;
    }
    case CallStatus::CALL_STATE_HELD:
      if (iter != active_calls_.end()) {
        iter->second.SetCallBackground();
        if (iter->second.timeout_serial != std::nullopt) {
          thread_looper_->CancelSerial(*(iter->second.timeout_serial));
        }
      }
      break;
    case CallStatus::CALL_STATE_HANGUP:
      if (iter != active_calls_.end()) {
        auto client = iter->second.remote_client;
        if (client != std::nullopt) {
          CloseRemoteConnection(*client);
        }
        if (iter->second.timeout_serial != std::nullopt) {
          thread_looper_->CancelSerial(*(iter->second.timeout_serial));
        }
        active_calls_.erase(iter);
      }
      break;
    case CallStatus::CALL_STATE_INCOMING: {
      CallStatus call_status(number);
      call_status.is_remote_call = true;
      call_status.is_voice_mode = mode;
      call_status.is_multi_party = mpty;
      call_status.is_mobile_terminated = true;
      call_status.is_international = num_type;
      call_status.remote_client = client.client_fd;
      call_status.call_state = CallStatus::CALL_STATE_INCOMING;

      auto index = last_active_call_index_++;
      active_calls_[index] = call_status;
      break;
    }
    default:  // Unsupported call state
      return;
  }
  thread_looper_->Post(makeSafeCallback(this, &CallService::CallStateUpdate));
}

}  // namespace cuttlefish

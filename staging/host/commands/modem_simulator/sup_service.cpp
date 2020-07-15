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

#include "sup_service.h"

namespace cuttlefish {

SupService::SupService(int32_t service_id, ChannelMonitor* channel_monitor,
                       ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper) {
  InitializeServiceState();
}

std::vector<CommandHandler> SupService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      CommandHandler("+CUSD",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleUSSD(client, cmd);
                     }),
      CommandHandler("+CLIR",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleCLIR(client, cmd);
                     }),
      CommandHandler("+CCWA",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleCallWaiting(client, cmd);
                     }),
      CommandHandler(
          "+CLIP?", [this](const Client& client) { this->HandleCLIP(client); }),
      CommandHandler("+CCFCU",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleCallForward(client, cmd);
                     }),
      CommandHandler("+CSSN",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSuppServiceNotifications(client, cmd);
                     }),
  };
  return (command_handlers);
}

void SupService::InitializeServiceState() {
  call_forward_infos_ = {
    CallForwardInfo(CallForwardInfo::Reason::CFU),
    CallForwardInfo(CallForwardInfo::Reason::CFB),
    CallForwardInfo(CallForwardInfo::Reason::CFNR),
    CallForwardInfo(CallForwardInfo::Reason::CFNRC)
  };
}

/**
 * AT+CUSD
 *   This command allows control of the Unstructured Supplementary Service Data (USSD)
 * according to 3GPP TS 22.090 [23], 3GPP TS 24.090 [148] and 3GPP TS 24.390 [131].
 * Both network and mobile initiated operations are supported.
 *
 * Command                        Possible response(s)
 * +CUSD=[<n>[,<str>[,<dcs>]]]      +CME ERROR: <err>
 * +CUSD?                           +CUSD: <n>
 *
 * <n>: integer type (sets/shows the result code presentation status to the TE).
 *   0 disable the result code presentation to the TE
 *   1 enable the result code presentation to the TE
 *   2 cancel session (not applicable to read command response)
 * <str>: string type USSD string
 *   when <str> parameter is not given, network is not interrogated
 * <dcs>: integer type (shows Cell Broadcast Data Coding Scheme, see 3GPP TS 23.038 [25]).
 *   Default value is 0.
 *
 * see RIL_REQUEST_SEND_USSD or RIL_REQUEST_CANCEL_USSD in RIL
 */
void SupService::HandleUSSD(const Client& client, std::string& /*command*/) {
  client.SendCommandResponse("OK");
}

/**
 * AT+CLIR
 *   This command refers to CLIR‑service according to 3GPP TS 22.081 that allows
 * a calling subscriber to enable or disable the presentation of the CLI to the
 * called party when originating a call.
 *
 * Command                        Possible response(s)
 * +CLIR: <n>
 * +CLIR?                         +CLIR: <n>,<m>
 *
 * <n>: integer type (parameter sets the adjustment for outgoing calls).
 *   0 presentation indicator is used according to the subscription of the CLIR service
 *   1 CLIR invocation
 *   2 CLIR suppression
 * <m>: integer type (parameter shows the subscriber CLIR / OIR service status in the network).
 *   0 CLIR / OIR not provisioned
 *   1 CLIR / OIR provisioned in permanent mode
 *   2 unknown (e.g. no network, etc.)
 *   3 CLIR / OIR temporary mode presentation restricted
 *   4 CLIR / OIR temporary mode presentation allowed
 *
 * see RIL_REQUEST_SET_CLIR or RIL_REQUEST_GET_CLIR in RIL
 */
void SupService::HandleCLIR(const Client& client, std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  if (*cmd == "AT+CLIR?") {
    ss << "+CLIR:" << clir_status_.type << "," << clir_status_.status;
    responses.push_back(ss.str());
  } else {
    clir_status_.type = (ClirStatusInfo::ClirType)cmd.GetNextInt();
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CLIP
 *   This command refers to the supplementary service CLIP (Calling Line
 * Identification Presentation) according to 3GPP TS 22.081 [3] and OIP
 * (Originating Identification Presentation) according to 3GPP TS 24.607 [119]
 * that enables a called subscriber to get the calling line identity (CLI) of
 * the calling party when receiving a mobile terminated call.
 *
 * Command                        Possible response(s)
 * +CLIP?                         +CLIP: <n>,<m>
 *
 * <n>: integer type (parameter sets/shows the result code presentation status to the TE).
 *   0 disable
 *   1 enable
 * <m>: integer type (parameter shows the subscriber CLIR / OIR service status in the network).
 *   0 CLIP / OIP not provisioned
 *   1 CLIP / OIP provisioned
 *   2 unknown (e.g. no network, etc.)
 *
 * see RIL_REQUEST_QUERY_CLIP in RIL
 */
void SupService::HandleCLIP(const Client& client) {
  std::vector<std::string> responses = {"+CLIP: 0, 0", "OK"};
  client.SendCommandResponse(responses);
}

/**
 * AT+CSSN
 *   This command refers to supplementary service related network initiated
 * notifications. The set command enables/disables the presentation of
 * notification result codes from TA to TE.
 *
 * Command                        Possible response(s)
 * +CSSN: [<n>[,<m>]]
 *
 * <n>: integer type (parameter sets/shows the +CSSI intermediate result code
 *                    presentation status to the TE)
 *   0   disable
 *   1   enable
 * <m>: integer type (parameter sets/shows the +CSSU unsolicited result code
 *                    presentation status to the TE)
 *   0   disable
 *   1   enable
 *
 * see RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION in RIL
 */
void SupService::HandleSuppServiceNotifications(const Client& client, std::string& /*command*/) {
  client.SendCommandResponse("OK");
}

/**
 * AT+CCFCU
 *   The command allows control of the communication forwarding supplementary service
 * according to 3GPP TS 22.072 [31], 3GPP TS 22.082 [4] and 3GPP TS 24.604 [132].
 *
 * Command                            Possible response(s)
 * +CCFCU=<reason>,<mode>               +CME ERROR: <err>
 * [,<numbertype>,<ton>,<number>        when <mode>=2 and command successful:
 * [,<class>,<ruleset>                  +CCFCU: <status>,<class1>[,<numbertype>,
 * [,<subaddr>[,<satype>[,<time>]]]]]           <ton>,<number>[,<subaddr>,<satype>[,<time>]]]
 * [,<class>,<ruleset>
 *
 * see SupService::CallForwardInfo
 *
 * see RIL_REQUEST_SET_CALL_FORWARD or RIL_REQUEST_QUERY_CALL_FORWARD_STATUS in RIL
 */
void SupService::HandleCallForward(const Client& client, std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;

  CommandParser cmd(command);
  cmd.SkipPrefix();

  int reason = cmd.GetNextInt();
  int status = cmd.GetNextInt();
  int number_type = cmd.GetNextInt();
  int ton = cmd.GetNextInt();
  std::string_view number = cmd.GetNextStr();
  int classx = cmd.GetNextInt();

  switch (reason) {
    case CallForwardInfo::Reason::ALL_CF: {
      if (status == CallForwardInfo::CallForwardInfoStatus::INTERROGATE) {
        auto iter = call_forward_infos_.begin();
        for (; iter != call_forward_infos_.end(); ++iter) {
          ss.clear();
          ss << "+CCFCU: " << iter->status << "," << classx << "," << number_type
                  << "," << ton << ",\"" << iter->number << "\"";
          if (iter->reason == CallForwardInfo::Reason::CFNR) {
            ss << ",,," << iter->timeSeconds;
          }
          responses.push_back(ss.str());
          ss.str("");
        }
      }
      break;
    }
    case CallForwardInfo::Reason::CFU:
    case CallForwardInfo::Reason::CFB:
    case CallForwardInfo::Reason::CFNR:
    case CallForwardInfo::Reason::CFNRC: {
      if (status == CallForwardInfo::CallForwardInfoStatus::INTERROGATE) {
        ss << "+CCFCU: " << call_forward_infos_[reason].status
           << "," << classx << "," << number_type << "," << ton << ",\""
           << call_forward_infos_[reason].number << "\"";
        if (reason == CallForwardInfo::Reason::CFNR) {
          ss << ",,," << call_forward_infos_[reason].timeSeconds;
        }
        responses.push_back(ss.str());
      } else {
        if (status == CallForwardInfo::CallForwardInfoStatus::REGISTRATION) {
          call_forward_infos_[reason].status
                = CallForwardInfo::CallForwardInfoStatus::ENABLE;
        } else {
          call_forward_infos_[reason].status =
                (CallForwardInfo::CallForwardInfoStatus)status;
        }
        call_forward_infos_[reason].number_type = number_type;
        call_forward_infos_[reason].ton = ton;
        call_forward_infos_[reason].number = number;
        if (reason == CallForwardInfo::Reason::CFNR) {
          cmd.SkipComma();
          cmd.SkipComma();
          cmd.SkipComma();
          call_forward_infos_[reason].timeSeconds = cmd.GetNextInt();
        }
      }
      break;
    }
    default:
      client.SendCommandResponse(kCmeErrorInCorrectParameters);
      return;
  }

  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CCWA
 *   This command allows control of the supplementary service Call Waiting
 * according to 3GPP TS 22.083 [5] and Communication Waiting according to
 * 3GPP TS 24.607 [137]. Activation, deactivation and status query are supported.
 *
 * Command                        Possible response(s)
 * +CCWA=[<n>[,<mode>[,<class>]]] +CME ERROR: <err>
 *                                when <mode>=2 and command successful
                                  +CCWA: <status>,<class1>
                                      [<CR><LF>+CCWA: <status>,<class2>
 * <n>: integer type (sets/shows the result code presentation status to the TE).
 *  0   disable
 *  1   enable
 * <mode>: integer type (when <mode> parameter is not given, network is not interrogated).
 *  0   disable
 *  1   enable
 *  2   query status
 * <classx>: a sum of integers each representing a class of information
 *           (default 7 - voice, data and fax).
 * <status>: integer type
 *  0   not active
 *  1   active
 *
 * see RIL_REQUEST_QUERY_CALL_WAITING and RIL_REQUEST_SET_CALL_WAITING in RIL
 */
void SupService::HandleCallWaiting(const Client& client, std::string& command) {
  std::vector<std::string> responses;
  std::stringstream ss;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  cmd.SkipComma();
  int mode = cmd.GetNextInt();
  int classx = cmd.GetNextInt();

  if (mode == 2) {  // Query
    if (classx == -1) {
      classx = 7;
    }
    ss << "+CCWA: " << call_waiting_info_.mode << "," << classx;
    responses.push_back(ss.str());
  } else if (mode == 0 || mode == 1) {  // Enable or disable
    call_waiting_info_.mode = mode;
    if (classx != -1) {
      call_waiting_info_.classx = classx;
    }
  }

  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

}  // namespace cuttlefish

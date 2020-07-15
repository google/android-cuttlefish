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

#include "sms_service.h"
#include "pdu_parser.h"

namespace cuttlefish {

SmsService::SmsService(int32_t service_id, ChannelMonitor* channel_monitor,
                       ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper) {
  InitializeServiceState();
}

std::vector<CommandHandler> SmsService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      CommandHandler("+CMGS",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSendSMS(client, cmd);
                     }),
      CommandHandler("+CNMA",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSMSAcknowledge(client, cmd);
                     }),
      CommandHandler("+CMGW",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleWriteSMSToSim(client, cmd);
                     }),
      CommandHandler("+CMGD",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleDeleteSmsOnSim(client, cmd);
                     }),
      CommandHandler("+CSCB",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleBroadcastConfig(client, cmd);
                     }),
      CommandHandler(
          "+CSCA?",
          [this](const Client& client) { this->HandleGetSmscAddress(client); }),
      CommandHandler("+CSCA=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSetSmscAddress(client, cmd);
                     }),
      CommandHandler("+REMOTESMS",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleReceiveRemoteSMS(client, cmd);
                     }),
  };
  return (command_handlers);
}

void SmsService::InitializeServiceState() {
  is_waiting_sms_pdu_ = false;
  is_waiting_sms_to_sim_ = false;
  message_id_ = 1;
  message_reference_ = 1;

  broadcast_config_ = {0, "", ""};
}

void SmsService::SetupDependency(SimService* sim) { sim_service_ = sim; }

/**
 * AT+CMGS
 *   This command sends message from a TE to the network (SMS-SUBMIT).
 *
 * Command                            Possible response(s)
 * +CMGS=<length><CR>                  "> "
 * PDU is given<ctrl-Z/ESC>            +CMGS: <mr>[,<ackpdu>]<CR>OK
 *                                     +CMS ERROR: <err>
 *
 * <length>:must indicate the number of octets coded in the TP
 *          layer data unit to be given.
 *
 * see RIL_REQUEST_SEND_SMS in RIL
 */
void SmsService::HandleSendSMS(const Client& client, std::string& /*command*/) {
  is_waiting_sms_pdu_ = true;

  std::vector<std::string> responses;
  responses.push_back("> ");
  client.SendCommandResponse(responses);
}

/**
 * AT+CNMA
 *   This command confirms reception of a new message (SMS-DELIVER or
 * SMS-STATUS-REPORT) which is routed directly to the TE.
 *
 * Command                            Possible response(s)
 * +CNMA [=<n>[, <length> [<CR>        OK
 * PDU is given<ctrl-Z/ESC>]]]         +CMS ERROR: <err>
 *
 * <n>: integer type
 *   0: command operates similarly as defined for the text mode
 *   1: send RP-ACK
 *   2: send RP-ERROR
 * <length>: ACKPDU length（octet)
 *
 * see RIL_REQUEST_SMS_ACKNOWLEDGE in RIL
 */
void SmsService::HandleSMSAcknowledge(const Client& client, std::string& /*command*/) {
  client.SendCommandResponse("OK");
}

/*
 * AT+CMGW
 *   This command stores message (either SMS-DELIVER or SMS-SUBMIT)
 * to memory storage <mem2>.
 *
 * Command                            Possible response(s)
 * +CMGW=<length>[,<stat>]<CR>         "> "
 * PDU is given <ctrl-Z/ESC>           +CMGW: <index>
 *                                     +CMS ERROR: <err>
 * <length>: the length of TPDU(bit) with a range of 9-160
 * < stat >: integer:
 *        0: Unread Message. (MT)
 *        1: Read Message. (MT)
 *        2: Unsent Message. (MO)
 *        3: Sent Message. (MO)
 * < index>: index id of <mem2>
 *
 * see RIL_REQUEST_WRITE_SMS_TO_SIM in RIL
 */
void SmsService::HandleWriteSMSToSim(const Client& client, std::string& command) {
  is_waiting_sms_to_sim_ = true;

  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip "AT+CMGW="
  cmd.SkipComma();
  sms_status_on_sim_ = (SmsMessage::SmsStatus)cmd.GetNextInt();
  client.SendCommandResponse("> ");
}

/**
 * AT+CMGD
 *   This command deletes message from preferred message storage <mem1>
 * location <index>.
 *
 * Command                            Possible response(s)
 * +CMGD=<index>[, <DelFlag>]          OK
 *                                     +CMS ERROR: <err>
 * < index>: index id of <mem2>
 *
 * see RIL_REQUEST_DELETE_SMS_ON_SIM in RIL
 */
void SmsService::HandleDeleteSmsOnSim(const Client& client, std::string& command) {
  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip "AT+CMGD="

  int index = cmd.GetNextInt();
  auto iter = messages_on_sim_card_.find(index);
  if (iter == messages_on_sim_card_.end()) {
    client.SendCommandResponse(kCmeErrorInvalidIndex);  // No such message
    return;
  }

  messages_on_sim_card_.erase(iter);
  client.SendCommandResponse("OK");
}

/**
 * AT+CSCB
 *   Set command selects which types of CBMs are to be received by the ME.
 *
 * Command                            Possible response(s)
 * +CSCB=[<mode>[,<mids>[,<dcss>]]]    OK
 * +CSCB?                              +CSCB: <mode>,<mids>,<dcss>
 *
 * <mode>:
 *      0: message types specified in <mids> and <dcss> are accepted
 *      1: message types specified in <mids> and <dcss> are not accepted
 * <mids>: string type; all different possible combinations of CBM message
 *         identifiers (refer <mid>).
 * <dcss>: string type; all different possible combinations of CBM data coding
 *         schemes.
 *
 * see RIL_REQUEST_GSM_GET_BROADCAST_SMS_CONFIG &
 *     RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG in RIL
 * Notes: This command is allowed in TEXT mode.
 */
void SmsService::HandleBroadcastConfig(const Client& client, std::string& command) {
  std::vector<std::string> responses;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  if (*cmd == "AT+CSCB?") {  // Query
    std::stringstream ss;
    ss << "+CSCB: " << broadcast_config_.mode << ","
                    << broadcast_config_.mids << ","
                    << broadcast_config_.dcss;
    responses.push_back(ss.str());
  } else {  // Set
    broadcast_config_.mode = cmd.GetNextInt();
    broadcast_config_.mids = cmd.GetNextStr();
    broadcast_config_.dcss = cmd.GetNextStr();
  }
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/**
 * AT+CSCA
 *   Set command updates the SMSC address, through which mobile originated
 * SMs are transmitted.
 *
 * Command                            Possible response(s)
 * +CSCA=<sca>[,<tosca>]               OK
 * +CSCA?                              +CSCA: <sca>,<tosca>
 *
 * <sca>: service center address, its maximum length is 20.
 * <tosca>: service center address format,protocol uses 8-bit address integer.
 *
 * see RIL_REQUEST_SET_SMSC_ADDRESS in RIL
 */
void SmsService::HandleGetSmscAddress(const Client& client) {
  std::vector<std::string> responses;

  std::stringstream ss;
  ss << "+CSCA: " << sms_service_center_address_.sca << ","
                  << sms_service_center_address_.tosca;
  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

void SmsService::HandleSetSmscAddress(const Client& client, std::string& command) {
  CommandParser cmd(command);
  cmd.SkipPrefix();  // skip "AT+CSCA="

  sms_service_center_address_.sca = cmd.GetNextStr();
  sms_service_center_address_.tosca = cmd.GetNextInt();

  client.SendCommandResponse("OK");
}

void SmsService::SendSmsToRemote(std::string remote_port, PDUParser& sms_pdu) {
  auto remote_client = ConnectToRemoteCvd(remote_port);
  if (!remote_client->IsOpen()) {
    return;
  }

  auto local_host_port = GetHostPort();
  auto pdu = sms_pdu.CreateRemotePDU(local_host_port);

  std::string command = "AT+REMOTESMS=" + pdu + "\r";
  std::string token = "REM0";
  remote_client->Write(token.data(), token.size());
  remote_client->Write(command.data(), command.size());
}

/* process AT+CMGS PDU */
void SmsService::HandleSendSMSPDU(const Client& client, std::string& command) {
  is_waiting_sms_pdu_ = false;

  std::vector<std::string> responses;
  PDUParser sms_pdu(command);
  if (!sms_pdu.IsValidPDU()) {
    /* Invalid PDU mode parameter */
    client.SendCommandResponse(kCmsErrorInvalidPDUModeParam);
    return;
  }

  std::string phone_number = sms_pdu.GetPhoneNumberFromAddress();

  int port = 0;
  if (phone_number.length() == 11) {
    port = std::stoi(phone_number.substr(7));
  } else if (phone_number.length() == 4) {
    port = std::stoi(phone_number);
  }

  if (phone_number == "") {  /* Phone number unknown */
    LOG(ERROR) << "Failed to get phone number form address";
    client.SendCommandResponse(kCmsErrorSCAddressUnknown);
    return;
  } else if (port >= kRemotePortRange.first &&
             port <= kRemotePortRange.second) {
    std::stringstream ss;
    ss << port;
    auto remote_host_port = ss.str();
    if (GetHostPort() == remote_host_port) {  // Send SMS to local host port
      thread_looper_->PostWithDelay(
          std::chrono::seconds(1),
          makeSafeCallback<SmsService>(
              weak_from_this(),
              [&sms_pdu](SmsService* me) { me->HandleReceiveSMS(sms_pdu); }));
    } else {  // Send SMS to remote host port
      SendSmsToRemote(remote_host_port, sms_pdu);
    }
  } else if (sim_service_ && phone_number == sim_service_->GetPhoneNumber()) {
    /* Local phone number */
    thread_looper_->PostWithDelay(
        std::chrono::seconds(1),
        makeSafeCallback<SmsService>(
            weak_from_this(),
            [sms_pdu](SmsService* me) { me->HandleReceiveSMS(sms_pdu); }));
  } /* else pretend send SMS success */

  std::stringstream ss;
  ss << "+CMGS: " << ++message_reference_;
  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);

  if (sms_pdu.IsNeededStatuReport()) {
    int ref = message_reference_;
    thread_looper_->PostWithDelay(
        std::chrono::seconds(1),
        makeSafeCallback<SmsService>(weak_from_this(),
                                     [sms_pdu, ref](SmsService* me) {
                                       me->HandleSMSStatuReport(sms_pdu, ref);
                                     }));
  }
}

/* AT+CMGS callback function */
void SmsService::HandleReceiveSMS(PDUParser sms_pdu) {
  std::string pdu = sms_pdu.CreatePDU();
  if (pdu != "") {
    SendUnsolicitedCommand("+CMT: 0");
    SendUnsolicitedCommand(pdu);
  }
}

/* Process AT+CMGW PDU */
void SmsService::HandleWriteSMSPduToSim(const Client& client, std::string& command) {
  is_waiting_sms_to_sim_ = false;

  SmsMessage message;
  message.status = sms_status_on_sim_;
  message.message = command;
  int index = message_id_++;
  messages_on_sim_card_[index] = message;

  std::vector<std::string> responses;
  std::stringstream ss;
  ss << "+CMGW: " << index;
  responses.push_back(ss.str());
  responses.push_back("OK");
  client.SendCommandResponse(responses);
}

/* SMS Status Report */
void SmsService::HandleSMSStatuReport(PDUParser sms_pdu, int message_reference) {
  std::string response;
  std::stringstream ss;

  auto pdu = sms_pdu.CreateStatuReport(message_reference);
  auto pdu_length = (pdu.size() - 2) / 2;  // Not Including SMSC Address
  if (pdu != "" && pdu_length > 0) {
    ss << "+CDS: " << pdu_length;
    SendUnsolicitedCommand(ss.str());
    SendUnsolicitedCommand(pdu);
  }
}

/* AT+REMOTESMS=PDU */
void SmsService::HandleReceiveRemoteSMS(const Client& /*client*/, std::string& command) {
  CommandParser cmd(command);
  cmd.SkipPrefix();

  std::string pdu(*cmd);
  PDUParser sms_pdu(pdu);
  if (!sms_pdu.IsValidPDU()) {
    LOG(ERROR) << "Failed to decode PDU";
    return;
  }
  pdu = sms_pdu.CreatePDU();
  if (pdu != "") {
    SendUnsolicitedCommand("+CMT: 0");
    SendUnsolicitedCommand(pdu);
  }
}
}  // namespace cuttlefish

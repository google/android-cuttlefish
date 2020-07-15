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

#include "modem_service.h"
#include "sim_service.h"
#include "pdu_parser.h"

namespace cuttlefish {

class SmsService : public ModemService , public std::enable_shared_from_this<SmsService> {
 public:
  SmsService(int32_t service_id, ChannelMonitor* channel_monitor,
             ThreadLooper* thread_looper);
  ~SmsService() = default;

  SmsService(const SmsService &) = delete;
  SmsService &operator=(const SmsService &) = delete;

  void SetupDependency(SimService* sim);

  void HandleSendSMS(const Client& client, std::string& command);
  void HandleSendSMSPDU(const Client& client, std::string& command);
  void HandleSMSAcknowledge(const Client& client, std::string& command);
  void HandleWriteSMSToSim(const Client& client, std::string& command);
  void HandleDeleteSmsOnSim(const Client& client, std::string& command);
  void HandleBroadcastConfig(const Client& client, std::string& command);
  void HandleGetSmscAddress(const Client& client);
  void HandleSetSmscAddress(const Client& client, std::string& command);
  void HandleWriteSMSPduToSim(const Client& client, std::string& command);
  void HandleReceiveRemoteSMS(const Client& client, std::string& command);

  bool IsWaitingSmsPdu() { return is_waiting_sms_pdu_; }
  bool IsWaitingSmsToSim() { return is_waiting_sms_to_sim_; }

 private:
  void InitializeServiceState();
  std::vector<CommandHandler> InitializeCommandHandlers();

  void HandleReceiveSMS(PDUParser sms_pdu);
  void HandleSMSStatuReport(PDUParser sms_pdu, int message_reference);
  void SendSmsToRemote(std::string remote_port, PDUParser& sms_pdu);

  SimService* sim_service_;

  struct SmsMessage {
    enum SmsStatus { kUnread = 0, kRead = 1, kUnsent = 2, kSent = 3 };

    std::string message;
    SmsStatus status;
  };

  struct BroadcastConfig {
    int mode;
    std::string mids;
    std::string dcss;
  };

  struct SmsServiceCenterAddress {
    std::string sca;
    int tosca;
  };

  bool is_waiting_sms_pdu_;
  bool is_waiting_sms_to_sim_;
  int message_id_;
  int message_reference_;
  SmsMessage::SmsStatus sms_status_on_sim_;

  BroadcastConfig broadcast_config_;
  SmsServiceCenterAddress sms_service_center_address_;

  std::map<int, SmsMessage> messages_on_sim_card_;
};

}  // namespace cuttlefish

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

namespace cuttlefish {

class StkService : public ModemService, public std::enable_shared_from_this<StkService> {
 public:
  StkService(int32_t service_id, ChannelMonitor* channel_monitor,
             ThreadLooper* thread_looper);
  ~StkService() = default;

  StkService(const StkService &) = delete;
  StkService &operator=(const StkService &) = delete;

  void SetupDependency(SimService* sim);

  void HandleReportStkServiceIsRunning(const Client& client);
  void HandleSendEnvelope(const Client& client, std::string& command);
  void HandleSendTerminalResponseToSim(const Client& client, std::string& command);

 private:
  std::vector<CommandHandler> InitializeCommandHandlers();

  SimService* sim_service_;

  // For now, only support DISPLAY_TEXT, SELECT_ITEM and SETUP_MENU
  enum CommandType {
    DISPLAY_TEXT        = 0x21,
    GET_INKEY           = 0x22,
    GET_INPUT           = 0x23,
    LAUNCH_BROWSER      = 0x15,
    PLAY_TONE           = 0x20,
    REFRESH             = 0x01,
    SELECT_ITEM         = 0x24,
    SEND_SS             = 0x11,
    SEND_USSD           = 0x12,
    SEND_SMS            = 0x13,
    RUN_AT              = 0x34,
    SEND_DTMF           = 0x14,
    SET_UP_EVENT_LIST   = 0x05,
    SET_UP_IDLE_MODE_TEXT = 0x28,
    SET_UP_MENU         = 0x25,
    SET_UP_CALL         = 0x10,
    PROVIDE_LOCAL_INFORMATION = 0x26,
    LANGUAGE_NOTIFICATION = 0x35,
    OPEN_CHANNEL        = 0x40,
    CLOSE_CHANNEL       = 0x41,
    RECEIVE_DATA        = 0x42,
    SEND_DATA           = 0x43,
    GET_CHANNEL_STATUS  = 0x44
  };

  std::vector<std::string> current_select_item_menu_ids_;

  XMLElement* GetCurrentSelectItem();
  void OnUnsolicitedCommandForTR(std::string& command);
};

}  // namespace cuttlefish

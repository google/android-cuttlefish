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

#include "stk_service.h"

namespace cuttlefish {

StkService::StkService(int32_t service_id, ChannelMonitor* channel_monitor,
                       ThreadLooper* thread_looper)
    : ModemService(service_id, this->InitializeCommandHandlers(),
                   channel_monitor, thread_looper) {}

std::vector<CommandHandler> StkService::InitializeCommandHandlers() {
  std::vector<CommandHandler> command_handlers = {
      CommandHandler("+CUSATD?",
                     [this](const Client& client) {
                       this->HandleReportStkServiceIsRunning(client);
                     }),
      CommandHandler("+CUSATE=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSendEnvelope(client, cmd);
                     }),
      CommandHandler("+CUSATT=",
                     [this](const Client& client, std::string& cmd) {
                       this->HandleSendTerminalResponseToSim(client, cmd);
                     }),
  };
  return (command_handlers);
}

void StkService::SetupDependency(SimService* sim) { sim_service_ = sim; }

/**
 * AT+CUSATD
 *   This command determines if, and optionally which profile should be downloaded
 * to the UICC automatically upon start-up.
 *
 * Command                             Possible response(s)
 * +CUSATD=[<download>[,<reporting>]]  +CME ERROR: <err>
 * +CUSATD?                            +CUSATD: <download>,<reporting>
 *
 * <download>: integer type.
 *   0   Download MT default profile automatically during next start-up.
 *   1   Download the combined TE and MT profile
 *   2   Halt next UICC start-up when ready for profile download.
 * <reporting>: integer type.
 *   0   Disable +CUSATS, i.e. no notification.
 *   1   Enable +CUSATS, i.e. notify TE.
 *
 * see RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING in RIL
 */
void StkService::HandleReportStkServiceIsRunning(const Client& client) {
  std::vector<std::string> response = {{"+CUSATD: 0,1"}, {"OK"}};
  client.SendCommandResponse(response);

  if (!sim_service_) return;

  XMLElement *root = sim_service_->GetIccProfile();
  if (!root) return;

  XMLElement *setup_menu = root->FirstChildElement("SETUPMENU");
  auto text = setup_menu->FindAttribute("text");

  std::string unsol_command = "+CUSATP:";
  unsol_command += text ? text->Value() : "";
  SendUnsolicitedCommand(unsol_command);
}

/**
 * AT+CUSATE
 *   Execution command allows the TE to send a USAT envelope command to the MT
 *
 * Command                      Possible response(s)
 * +CUSATE=<envelope_command>   +CUSATE: <envelope_response>[,<busy>]
 *                              [<CR><LF>+CUSATE2: <sw1>,<sw2>]
 *                              +CME ERROR: <err>
 *
 * <envelope_command>: string type in hexadecimal character format.
 * <envelope_response>: string type in hexadecimal character format.
 * <busy>: integer type.
 *   0   UICC indicated normal ending of the command.
 *   1   UICC responded with USAT is busy, no retry by the MT.
 *   2   UICC responded with USAT is busy even after one or more retries by the MT.
 * <sw1>: integer type.
 * <sw2>: integer type.
 *
 * see RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND in RIL
 */
void StkService::HandleSendEnvelope(const Client& client , std::string& command) {
  std::vector<std::string> response = {{"+CUSATE: 0"}, {"OK"}};
  client.SendCommandResponse(response);

  if (!sim_service_) return;

  CommandParser cmd(command);
  cmd.SkipPrefix();
  auto data = cmd.GetNextStr();
  std::string menu_id(data.substr(data.size() - 2));  // get the last two char

  XMLElement *root = sim_service_->GetIccProfile();
  if (!root) return;

  XMLElement *setup_menu = root->FirstChildElement("SETUPMENU");
  if (!setup_menu) return;

  auto select_item = setup_menu->FirstChildElement("SELECTITEM");
  while (select_item) {
    auto menu_id_attr = select_item->FindAttribute("menuId");
    if (menu_id_attr && menu_id_attr->Value() == menu_id) {
      break;
    }
    select_item = select_item->NextSiblingElement("SELECTITEM");
  }
  if (!select_item) {
    LOG(ERROR) << "Not found menu id: " << menu_id;
    return;
  }

  auto select_item_cmd = select_item->FindAttribute("cmd");
  if (select_item_cmd) {
    std::string cmd_str = select_item_cmd->Value();
    if (cmd_str == "24") {  // SELECT_ITEM
      current_select_item_menu_ids_.push_back(menu_id);
    }
  }

  std::string unsol_command = "+CUSATP:";
  auto text = select_item->FindAttribute("text");
  std::string text_value = text ? text->Value() : "";
  unsol_command.append(text_value);
  SendUnsolicitedCommand(unsol_command);
}

/**
 * AT+CUSATT
 *   Execution command sends a USAT terminal response to the MT as an answer to
 * a preceding USAT proactive command sent from the UICC with unsolicited result
 * code +CUSATP: <proactive_command>
 *
 * Command                        Possible response(s)
 * +CUSATT=<terminal_response>    +CME ERROR: <err>
 *
 * <terminal_response>: string type in hexadecimal character format.
 *
 * see RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE in RIL
 */
void StkService::HandleSendTerminalResponseToSim(const Client& client, std::string& command) {
  std::vector<std::string> response = {{"+CUSATT: 0"}, {"OK"}};
  client.SendCommandResponse(response);

  OnUnsolicitedCommandForTR(command);
}

XMLElement* StkService::GetCurrentSelectItem() {
  if (!sim_service_) return nullptr;

  XMLElement *root = sim_service_->GetIccProfile();
  if (!root) {
    current_select_item_menu_ids_.clear();
    return nullptr;
  }

  XMLElement *menu = root->FirstChildElement("SETUPMENU");
  if (!menu) {
    current_select_item_menu_ids_.clear();
    return nullptr;
  }

  /**
   * e.g. current_select_item_menu_ids_: {"1", "02"}
   * <SELECTITEM id="1">
   *   <SELECTITEM id="01">
   *   </SELECTITEM>
   *   <SELECTITEM id="02">
   *   </SELECTITEM>
   * </SELECTITEM>
   */
  XMLElement* select_item = nullptr;
  auto iter = current_select_item_menu_ids_.begin();
  for (; iter != current_select_item_menu_ids_.end(); ++iter) {
    select_item = menu->FirstChildElement("SELECTITEM");
    while (select_item) {
      auto menu_id_attr = select_item->FindAttribute("menuId");
      if (menu_id_attr && menu_id_attr->Value() == *iter) {
        auto menu_id_str = menu_id_attr->Value();
        if (menu_id_str == *iter) break;
      }
      select_item = select_item->NextSiblingElement("SELECTITEM");
    }
    if (!select_item) break;
    menu = select_item;
  }

  return select_item;
}

void StkService::OnUnsolicitedCommandForTR(std::string& command) {
  CommandParser cmd(command);
  cmd.SkipPrefix();
  auto data = cmd.GetNextStr();
  auto menu_id = data.substr(data.size() - 2);

  // '10': UICC_SESSION_TERM_BY_USER
  // '12': NO_RESPONSE_FROM_USER
  if (menu_id == "10" || menu_id == "12") {
    current_select_item_menu_ids_.clear();
    SendUnsolicitedCommand("+CUSATEND");
    return;
  }

  XMLElement *select_item = GetCurrentSelectItem();
  if (!select_item) {
    current_select_item_menu_ids_.clear();
    SendUnsolicitedCommand("+CUSATEND");
    return;
  }

  if (menu_id == "11") {  // BACKWARD_MOVE_BY_USER
    current_select_item_menu_ids_.pop_back();
    if (current_select_item_menu_ids_.size() >= 1) {
      select_item = GetCurrentSelectItem();
      auto text = select_item->FindAttribute("text");
      if (text) {
        std::string unsol_command = "+CUSATP: ";
        unsol_command += text->Value();
        SendUnsolicitedCommand(unsol_command);
      }
    } else {
      SendUnsolicitedCommand("+CUSATEND");
    }
    return;
  } else if (menu_id == "00") {  // OK
    auto text = select_item->FindAttribute("text");
    if (text) {
      std::string unsol_command = "+CUSATP: ";
      unsol_command += text->Value();
      SendUnsolicitedCommand(unsol_command);
    }
    return;
  }

  auto final = select_item->FirstChildElement();
  while (final) {
    auto attr = final->FindAttribute("menuId");
    if (attr && attr->Value() == menu_id) {
      std::string attr_value = attr->Value();
      if (attr_value == menu_id) break;
    }
    final = final->NextSiblingElement();
  }
  if (!final) {
    current_select_item_menu_ids_.clear();
    SendUnsolicitedCommand("+CUSATEND");
    return;
  }

  auto cmd_attr = final->FindAttribute("cmd");
  if (cmd_attr) {
    std::string cmd_attr_str = cmd_attr->Value();
    if (cmd_attr_str == "24") {
      std::string menu_id_str(menu_id);
      current_select_item_menu_ids_.push_back(menu_id_str);
    }
  }
  auto text = final->FindAttribute("text");
  std::string unsol_command = "+CUSATP:";
  unsol_command += text ? text->Value() : "";
  SendUnsolicitedCommand(unsol_command);
}

}  // namespace cuttlefish

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

#include "host/commands/modem_simulator/modem_service.h"

#include <cstring>

#include "host/commands/modem_simulator/device_config.h"

namespace cuttlefish {

const std::string ModemService::kCmeErrorOperationNotAllowed      = "+CME ERROR: 3";
const std::string ModemService::kCmeErrorOperationNotSupported    = "+CME ERROR: 4";
const std::string ModemService::kCmeErrorSimNotInserted           = "+CME ERROR: 10";
const std::string ModemService::kCmeErrorSimPinRequired           = "+CME ERROR: 11";
const std::string ModemService::kCmeErrorSimPukRequired           = "+CME ERROR: 12";
const std::string ModemService::kCmeErrorSimBusy                  = "+CME ERROR: 14";
const std::string ModemService::kCmeErrorIncorrectPassword        = "+CME ERROR: 16";
const std::string ModemService::kCmeErrorMemoryFull               = "+CME ERROR: 20";
const std::string ModemService::kCmeErrorInvalidIndex             = "+CME ERROR: 21";
const std::string ModemService::kCmeErrorNotFound                 = "+CME ERROR: 22";
const std::string ModemService::kCmeErrorInvalidCharactersInTextString = "+CME ERROR: 27";
const std::string ModemService::kCmeErrorNoNetworkService         = "+CME ERROR: 30";
const std::string ModemService::kCmeErrorNetworkNotAllowedEmergencyCallsOnly = "+CME ERROR: 32";
const std::string ModemService::kCmeErrorInCorrectParameters      = "+CME ERROR: 50";
const std::string ModemService::kCmeErrorNetworkNotAttachedDueToMTFunctionalRestrictions = "+CME ERROR: 53";
const std::string ModemService::kCmeErrorFixedDialNumberOnlyAllowed = "+CME ERROR: 56";

const std::string ModemService::kCmsErrorOperationNotAllowed      = "+CMS ERROR: 302";
const std::string ModemService::kCmsErrorOperationNotSupported    = "+CMS ERROR: 303";
const std::string ModemService::kCmsErrorInvalidPDUModeParam      = "+CMS ERROR: 304";
const std::string ModemService::kCmsErrorSCAddressUnknown         = "+CMS ERROR: 304";

const std::pair<int, int> ModemService::kRemotePortRange =
    std::make_pair(6520, 6527);

CommandHandler::CommandHandler(const std::string& command, f_func handler)
    : command_prefix(command),
      match_mode(FULL_MATCH),
      f_command_handler(handler) {}

CommandHandler::CommandHandler(const std::string& command, p_func handler)
    : command_prefix(command),
      match_mode(PARTIAL_MATCH),
      p_command_handler(handler) {}

int CommandHandler::Compare(const std::string& command) const {
  int result = -1;
  if (match_mode == PARTIAL_MATCH) {
    result = command.compare(2, command_prefix.size(), command_prefix);  // skip "AT"
  } else {
    result = command.compare(2, command.size(), command_prefix);
  }
  return result;
}

void CommandHandler::HandleCommand(const Client& client,
                                   std::string& command) const {
  if (match_mode == PARTIAL_MATCH && p_command_handler != nullptr) {
    (*p_command_handler)(client, command);
  } else if (match_mode == FULL_MATCH && f_command_handler != nullptr) {
    (*f_command_handler)(client);
  } else {
    LOG(ERROR) << "Mismatched mode and handler, CHECK!";
  }
}

ModemService::ModemService(int32_t service_id,
                           std::vector<CommandHandler> command_handlers,
                           ChannelMonitor* channel_monitor,
                           ThreadLooper* thread_looper)
    : service_id_(service_id),
      command_handlers_(command_handlers),
      thread_looper_(thread_looper),
      channel_monitor_(channel_monitor) {}

bool ModemService::HandleModemCommand(const Client& client,
                                      std::string command) {
  for (auto& handler : command_handlers_) {
    if (handler.Compare(command) == 0) {
      handler.HandleCommand(client, command);
      return true;
    }
  }

  return false;
}

void ModemService::HandleCommandDefaultSupported(const Client& client) {
  std::string response{"OK\r"};
  client.SendCommandResponse(response);
}

void ModemService::SendUnsolicitedCommand(std::string unsol_command) {
  if (channel_monitor_) {
    channel_monitor_->SendUnsolicitedCommand(unsol_command);
    ;
  }
}

cuttlefish::SharedFD ModemService::ConnectToRemoteCvd(std::string port) {
  std::string remote_sock_name = "modem_simulator" + port;
  auto remote_sock = cuttlefish::SharedFD::SocketLocalClient(
      remote_sock_name.c_str(), true, SOCK_STREAM);
  if (!remote_sock->IsOpen()) {
    LOG(ERROR) << "Failed to connect to remote cuttlefish: " << port
               << ", error: " << strerror(errno);
  }
  return remote_sock;
}

void ModemService::SendCommandToRemote(cuttlefish::SharedFD remote_client, std::string response) {
  if (channel_monitor_) {
    channel_monitor_->SendRemoteCommand(remote_client, response);
    ;
  }
}

void ModemService::CloseRemoteConnection(cuttlefish::SharedFD remote_client) {
  if (channel_monitor_) {
    channel_monitor_->CloseRemoteConnection(remote_client);
    ;
  }
}

std::string ModemService::GetHostPort() {
  auto host_port = cuttlefish::modem::DeviceConfig::host_port();
  std::stringstream ss;
  ss << host_port;
  return ss.str();
}

}  // namespace cuttlefish

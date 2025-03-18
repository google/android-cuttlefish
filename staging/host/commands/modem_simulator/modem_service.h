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


#include <functional>
#include <map>
#include <optional>

#include "host/commands/modem_simulator/channel_monitor.h"
#include "host/commands/modem_simulator/command_parser.h"
#include "host/commands/modem_simulator/thread_looper.h"

namespace cuttlefish {

enum ModemServiceType : int {
  kSimService     = 0,
  kNetworkService = 1,
  kDataService    = 2,
  kCallService    = 3,
  kSmsService     = 4,
  kSupService     = 5,
  kStkService     = 6,
  kMiscService    = 7,
};

using f_func = std::function<void(const Client&)>;                // Full match
using p_func = std::function<void(const Client&, std::string&)>;  // Partial match

class CommandHandler {
 public:
  CommandHandler(const std::string& command, f_func handler);
  CommandHandler(const std::string& command, p_func handler);

  ~CommandHandler() = default;

  int Compare(const std::string& command) const;
  void HandleCommand(const Client& client, std::string& command) const;

 private:
  enum MatchMode {FULL_MATCH = 0, PARTIAL_MATCH = 1};

  std::string command_prefix;
  MatchMode match_mode;

  std::optional<f_func> f_command_handler;
  std::optional<p_func> p_command_handler;
};

class ModemService {
 public:

  virtual ~ModemService() = default;

  ModemService(const ModemService &) = delete;
  ModemService &operator=(const ModemService &) = delete;

  bool HandleModemCommand(const Client& client, std::string command);

  static constexpr char kCmeErrorOperationNotAllowed[] = "+CME ERROR: 3";
  static constexpr char kCmeErrorOperationNotSupported[] = "+CME ERROR: 4";
  static constexpr char kCmeErrorSimNotInserted[] = "+CME ERROR: 10";
  static constexpr char kCmeErrorSimPinRequired[] = "+CME ERROR: 11";
  static constexpr char kCmeErrorSimPukRequired[] = "+CME ERROR: 12";
  static constexpr char kCmeErrorSimBusy[] = "+CME ERROR: 14";
  static constexpr char kCmeErrorIncorrectPassword[] = "+CME ERROR: 16";
  static constexpr char kCmeErrorMemoryFull[] = "+CME ERROR: 20";
  static constexpr char kCmeErrorInvalidIndex[] = "+CME ERROR: 21";
  static constexpr char kCmeErrorNotFound[] = "+CME ERROR: 22";
  static constexpr char kCmeErrorInvalidCharactersInTextString[] =
      "+CME ERROR: 27";
  static constexpr char kCmeErrorNoNetworkService[] = "+CME ERROR: 30";
  static constexpr char kCmeErrorNetworkNotAllowedEmergencyCallsOnly[] =
      "+CME ERROR: 32";
  static constexpr char kCmeErrorInCorrectParameters[] = "+CME ERROR: 50";
  static constexpr char
      kCmeErrorNetworkNotAttachedDueToMTFunctionalRestrictions[] =
          "+CME ERROR: 53";
  static constexpr char kCmeErrorFixedDialNumberOnlyAllowed[] =
      "+CME ERROR: 56";

  static constexpr char kCmsErrorOperationNotAllowed[] = "+CMS ERROR: 302";
  static constexpr char kCmsErrorOperationNotSupported[] = "+CMS ERROR: 303";
  static constexpr char kCmsErrorInvalidPDUModeParam[] = "+CMS ERROR: 304";
  static constexpr char kCmsErrorSCAddressUnknown[] = "+CMS ERROR: 304";

  static constexpr std::pair<int, int> kRemotePortRange{6520, 6527};

  void CloseRemoteConnection(ClientId remote_client);

 protected:
  ModemService(int32_t service_id, std::vector<CommandHandler> command_handlers,
               ChannelMonitor* channel_monitor, ThreadLooper* thread_looper);
  void HandleCommandDefaultSupported(const Client& client);
  void SendUnsolicitedCommand(std::string unsol_command);

  cuttlefish::SharedFD ConnectToRemoteCvd(std::string port);
  void SendCommandToRemote(ClientId remote_client, std::string response);
  static std::string GetHostId();

  int32_t service_id_;
  const std::vector<CommandHandler> command_handlers_;
  ThreadLooper* thread_looper_;
  ChannelMonitor* channel_monitor_;
};

}  // namespace cuttlefish

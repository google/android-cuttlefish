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

#include <android-base/logging.h>

#include <functional>
#include <map>

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

  static const std::string kCmeErrorOperationNotAllowed;
  static const std::string kCmeErrorOperationNotSupported;
  static const std::string kCmeErrorSimNotInserted;
  static const std::string kCmeErrorSimPinRequired;
  static const std::string kCmeErrorSimPukRequired;
  static const std::string kCmeErrorSimBusy;
  static const std::string kCmeErrorIncorrectPassword;
  static const std::string kCmeErrorMemoryFull;
  static const std::string kCmeErrorInvalidIndex;
  static const std::string kCmeErrorNotFound;
  static const std::string kCmeErrorInvalidCharactersInTextString;
  static const std::string kCmeErrorNoNetworkService;
  static const std::string kCmeErrorNetworkNotAllowedEmergencyCallsOnly;
  static const std::string kCmeErrorInCorrectParameters;
  static const std::string kCmeErrorNetworkNotAttachedDueToMTFunctionalRestrictions;
  static const std::string kCmeErrorFixedDialNumberOnlyAllowed;

  static const std::string kCmsErrorOperationNotAllowed;
  static const std::string kCmsErrorOperationNotSupported;
  static const std::string kCmsErrorInvalidPDUModeParam;
  static const std::string kCmsErrorSCAddressUnknown;

  static const std::pair<int, int> kRemotePortRange;

 protected:
  ModemService(int32_t service_id, std::vector<CommandHandler> command_handlers,
               ChannelMonitor* channel_monitor, ThreadLooper* thread_looper);
  void HandleCommandDefaultSupported(const Client& client);
  void SendUnsolicitedCommand(std::string unsol_command);

  cuttlefish::SharedFD ConnectToRemoteCvd(std::string port);
  void SendCommandToRemote(cuttlefish::SharedFD remote_client,
                           std::string response);
  void CloseRemoteConnection(cuttlefish::SharedFD remote_client);
  static std::string GetHostPort();

  int32_t service_id_;
  const std::vector<CommandHandler> command_handlers_;
  ThreadLooper* thread_looper_;
  ChannelMonitor* channel_monitor_;
};

}  // namespace cuttlefish

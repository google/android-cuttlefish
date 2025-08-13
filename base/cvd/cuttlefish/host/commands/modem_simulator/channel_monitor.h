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

#include <thread>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/modem_simulator/client.h"

class ModemServiceTest;

namespace cuttlefish {

class ModemSimulator;

enum ModemSimulatorExitCodes : int {
  kSuccess = 0,
  kSelectError = 1,
  kServerError = 2,
};

class ChannelMonitor {
 public:
  ChannelMonitor(ModemSimulator& modem, cuttlefish::SharedFD server);
  ~ChannelMonitor();

  ChannelMonitor(const ChannelMonitor&) = delete;
  ChannelMonitor& operator=(const ChannelMonitor&) = delete;

  ClientId SetRemoteClient(SharedFD client, bool is_accepted);
  void SendRemoteCommand(ClientId client, std::string& response);
  void CloseRemoteConnection(ClientId client);

  // For modem services to send unsolicited commands
  void SendUnsolicitedCommand(std::string& response);

 private:
  ModemSimulator& modem_;
  std::thread monitor_thread_;
  cuttlefish::SharedFD server_;
  cuttlefish::SharedFD read_pipe_;
  cuttlefish::SharedFD write_pipe_;
  std::vector<std::unique_ptr<Client>> clients_;
  std::vector<std::unique_ptr<Client>> remote_clients_;

  void AcceptIncomingConnection();
  void OnClientSocketClosed(int sock);
  void ReadCommand(Client& client);

  void MonitorLoop();
  static void removeInvalidClients(
      std::vector<std::unique_ptr<Client>>& clients);
};

}  // namespace cuttlefish

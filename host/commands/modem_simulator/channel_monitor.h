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

#include <mutex>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_select.h"

namespace cuttlefish {

class ModemSimulator;

enum ModemSimulatorExitCodes : int {
  kSuccess = 0,
  kSelectError = 1,
  kServerError = 2,
};

/**
 * Client object managed by ChannelMonitor, contains two types, the RIL client
 * and the remote client of other cuttlefish instance.
 * Due to std::mutex does not implement its copy and operate= constructors, it
 * cann't be stored in standard contains (vector, map), so use the point instead.
 */
class Client {
 public:
  enum ClientType { RIL, REMOTE };

  ClientType type = RIL;
  cuttlefish::SharedFD client_fd;
  std::string incomplete_command;
  std::mutex write_mutex;
  bool first_read_command_;  // Only used when ClientType::REMOTE
  bool is_valid = true;

  Client() = default;
  ~Client() = default;
  Client(cuttlefish::SharedFD fd);
  Client(cuttlefish::SharedFD fd, ClientType client_type);
  Client(const Client& client) = delete;
  Client(Client&& client) = delete;

  Client& operator=(Client&& other) = delete;

  bool operator==(const Client& other) const;

  void SendCommandResponse(std::string response) const;
  void SendCommandResponse(const std::vector<std::string>& responses) const;
};

class ChannelMonitor {
 public:
  ChannelMonitor(ModemSimulator* modem, cuttlefish::SharedFD server);
  ~ChannelMonitor();

  ChannelMonitor(const ChannelMonitor&) = delete;
  ChannelMonitor& operator=(const ChannelMonitor&) = delete;

  void SetRemoteClient(cuttlefish::SharedFD client,  bool is_accepted);
  void SendRemoteCommand(cuttlefish::SharedFD client, std::string& response);
  void CloseRemoteConnection(cuttlefish::SharedFD client);

  // For modem services to send unsolicited commands
  void SendUnsolicitedCommand(std::string& response);

 private:
  ModemSimulator* modem_;
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
};

}  // namespace cuttlefish

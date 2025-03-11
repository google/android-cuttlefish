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

#include "host/commands/modem_simulator/channel_monitor.h"
#include "host/commands/modem_simulator/modem_service.h"
#include "host/commands/modem_simulator/nvram_config.h"
#include "host/commands/modem_simulator/thread_looper.h"

namespace cuttlefish {

class SimService;
class MiscService;
class NetworkService;
class SmsService;
class ModemSimulator {
 public:
  ModemSimulator(int32_t modem_id);
  ~ModemSimulator();

  ModemSimulator(const ModemSimulator&) = delete;
  ModemSimulator& operator=(const ModemSimulator&) = delete;

  void Initialize(std::unique_ptr<ChannelMonitor>&& channel_monitor);

  void DispatchCommand(const Client& client, std::string& command);

  void OnFirstClientConnected();
  void SaveModemState();
  bool IsWaitingSmsPdu();
  bool IsRadioOn() const;
  void SetRemoteClient(cuttlefish::SharedFD client, bool is_accepted) {
    channel_monitor_->SetRemoteClient(client, is_accepted);
  }

  void SetTimeZone(std::string timezone);

 private:
  int32_t modem_id_;
  std::unique_ptr<ChannelMonitor> channel_monitor_;
  std::unique_ptr<ThreadLooper> thread_looper_;

  SmsService* sms_service_{nullptr};
  SimService* sim_service_{nullptr};
  MiscService* misc_service_{nullptr};
  NetworkService* network_service_{nullptr};

  std::map<ModemServiceType, std::unique_ptr<ModemService>> modem_services_;

  static void LoadNvramConfig();

  void RegisterModemService();
};

}  // namespace cuttlefish

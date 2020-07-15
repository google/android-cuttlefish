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

#include "channel_monitor.h"
#include "thread_looper.h"
#include "modem_service.h"
#include "nvram_config.h"

namespace cuttlefish {

class ModemSimulator {
 public:
  ModemSimulator(int32_t modem_id);
  ~ModemSimulator() = default;

  ModemSimulator(const ModemSimulator&) = delete;
  ModemSimulator& operator=(const ModemSimulator&) = delete;

  void Initialize(std::unique_ptr<ChannelMonitor>&& channel_monitor);

  void DispatchCommand(const Client& client, std::string& command);

  void OnFirstClientConnected();
  void SaveModemState();
  bool IsWaitingSmsPdu();
  void SetRemoteClient(cuttlefish::SharedFD client, bool is_accepted) {
    channel_monitor_->SetRemoteClient(client, is_accepted);
  }

 private:
  int32_t modem_id_;
  std::unique_ptr<ChannelMonitor> channel_monitor_;
  ThreadLooper* thread_looper_;

  std::map<ModemServiceType, std::unique_ptr<ModemService>> modem_services_;

  static void LoadNvramConfig();

  void RegisterModemService();
};

}  // namespace cuttlefish

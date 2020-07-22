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

#include "host/commands/modem_simulator/modem_service.h"

namespace cuttlefish {

class MiscService : public ModemService, public std::enable_shared_from_this<MiscService>  {
 public:
  MiscService(int32_t service_id, ChannelMonitor* channel_monitor,
              ThreadLooper* thread_looper);
  ~MiscService() = default;

  MiscService(const MiscService &) = delete;
  MiscService &operator=(const MiscService &) = delete;

  void HandleGetIMEI(const Client& client, std::string& command);

  void TimeUpdate();

 private:
  std::vector<CommandHandler> InitializeCommandHandlers();
};

}  // namespace cuttlefish

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

#include <memory>
#include <string>
#include <vector>

#include <json/json.h>

#include "host/frontend/gcastv2/https/include/https/WebSocketHandler.h"
#include "host/frontend/gcastv2/signaling_server/device_registry.h"
#include "host/frontend/gcastv2/signaling_server/server_config.h"
#include "host/frontend/gcastv2/signaling_server/signal_handler.h"

namespace cvd {

class ClientHandler;

class DeviceHandler
    : public SignalHandler,
      public std::enable_shared_from_this<DeviceHandler> {
 public:
  DeviceHandler(DeviceRegistry* registry, const ServerConfig& server_config);
  ~DeviceHandler() override;

  Json::Value device_info() const { return device_info_; }

  size_t RegisterClient(std::shared_ptr<ClientHandler> client_handler);
  void SendClientMessage(size_t client_id, const Json::Value& message);
 protected:
  int handleMessage(const std::string& type,
                    const Json::Value& message) override;

 private:
  int HandleRegistrationRequest(const Json::Value& message);
  int HandleForward(const Json::Value& message);

  std::string device_id_;
  Json::Value device_info_;
  std::vector<std::weak_ptr<ClientHandler>> clients_;
};
}  // namespace cvd

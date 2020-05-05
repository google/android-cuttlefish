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

#include <json/json.h>

#include "host/frontend/gcastv2/https/include/https/WebSocketHandler.h"
#include "host/frontend/gcastv2/signaling_server/device_registry.h"
#include "host/frontend/gcastv2/signaling_server/server_config.h"
#include "host/frontend/gcastv2/signaling_server/signal_handler.h"

namespace cvd {
class DeviceHandler;
class ClientHandler : public SignalHandler,
                      public std::enable_shared_from_this<ClientHandler> {
 public:
  ClientHandler(DeviceRegistry* registry, const ServerConfig& server_config);
  void SendDeviceMessage(const Json::Value& message);
 protected:
  int handleMessage(const std::string& type,
                    const Json::Value& message) override;

 private:
  int handleConnectionRequest(const Json::Value& message);
  int handleForward(const Json::Value& message);

  std::weak_ptr<DeviceHandler> device_handler_;
  // The device handler assigns this to each client to be able to differentiate
  // them.
  size_t client_id_;
};
}  // namespace cvd

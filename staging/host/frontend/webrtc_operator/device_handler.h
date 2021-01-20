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

#include "host/frontend/webrtc_operator/device_registry.h"
#include "host/frontend/webrtc_operator/server_config.h"
#include "host/frontend/webrtc_operator/signal_handler.h"
#include "host/libs/websocket/websocket_handler.h"

namespace cuttlefish {

class ClientHandler;

class DeviceHandler : public SignalHandler,
                      public std::enable_shared_from_this<DeviceHandler> {
 public:
  DeviceHandler(struct lws* wsi, DeviceRegistry* registry,
                const ServerConfig& server_config);

  Json::Value device_info() const { return device_info_; }

  size_t RegisterClient(std::shared_ptr<ClientHandler> client_handler);
  void SendClientMessage(size_t client_id, const Json::Value& message);
  void SendClientDisconnectMessage(size_t client_id);

  void OnClosed() override;

 protected:
  void handleMessage(const std::string& type,
                    const Json::Value& message) override;

 private:
  void HandleRegistrationRequest(const Json::Value& message);
  void HandleForward(const Json::Value& message);

  std::string device_id_;
  Json::Value device_info_;
  std::vector<std::weak_ptr<ClientHandler>> clients_;
};

class DeviceHandlerFactory : public WebSocketHandlerFactory {
 public:
  DeviceHandlerFactory(DeviceRegistry* registry,
                       const ServerConfig& server_config);
  std::shared_ptr<WebSocketHandler> Build(struct lws* wsi) override;

 private:
  DeviceRegistry* registry_;
  const ServerConfig& server_config_;
};
}  // namespace cuttlefish

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

#include "host/frontend/webrtc_operator/device_registry.h"
#include "host/frontend/webrtc_operator/server_config.h"
#include "host/libs/websocket/websocket_handler.h"

namespace cuttlefish {

class SignalHandler : public WebSocketHandler {
 public:
  void OnReceive(const uint8_t* msg, size_t len, bool binary) override;
  void OnReceive(const uint8_t* msg, size_t len, bool binary,
                 bool is_final) override;
  void OnConnected() override;
 protected:
  SignalHandler(struct lws* wsi, DeviceRegistry* registry,
                const ServerConfig& server_config);

  virtual void handleMessage(const std::string& message_type,
                             const Json::Value& message) = 0;
  void SendServerConfig();

  void LogAndReplyError(const std::string& message);
  void Reply(const Json::Value& json);

  DeviceRegistry* registry_;
  const ServerConfig& server_config_;
  std::vector<uint8_t> receive_buffer_;
};
}  // namespace cuttlefish

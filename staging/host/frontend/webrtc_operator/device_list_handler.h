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

#include "host/libs/websocket/websocket_handler.h"
#include "host/frontend/webrtc_operator/device_registry.h"

namespace cuttlefish {

class DeviceListHandler : public WebSocketHandler {
 public:
  DeviceListHandler(struct lws* wsi, const DeviceRegistry& registry);

  void OnReceive(const uint8_t* msg, size_t len, bool binary) override;
  void OnConnected() override;
  void OnClosed() override;

 private:
  const DeviceRegistry& registry_;
};

class DeviceListHandlerFactory : public WebSocketHandlerFactory {
 public:
  DeviceListHandlerFactory(const DeviceRegistry& registry);
  std::shared_ptr<WebSocketHandler> Build(struct lws* wsi) override;

 private:
  const DeviceRegistry& registry_;
};
}  // namespace cuttlefish

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
#include "host/frontend/webrtc_operator/signal_handler.h"
#include "host/libs/websocket/websocket_handler.h"

namespace cuttlefish {
class DeviceHandler;

class ClientHandler {
 public:
  virtual ~ClientHandler() = default;
  virtual void SendDeviceMessage(const Json::Value& message) = 0;
};

class ClientWSHandler : public ClientHandler,
                        public SignalHandler,
                        public std::enable_shared_from_this<ClientHandler> {
 public:
  ClientWSHandler(struct lws* wsi, DeviceRegistry* registry,
                  const ServerConfig& server_config);

  void SendDeviceMessage(const Json::Value& message) override;

  void OnClosed() override;

 protected:
  void handleMessage(const std::string& type,
                     const Json::Value& message) override;

 private:
  void handleConnectionRequest(const Json::Value& message);
  void handleForward(const Json::Value& message);

  std::weak_ptr<DeviceHandler> device_handler_;
  // The device handler assigns this to each client to be able to differentiate
  // them.
  size_t client_id_;
};

class ClientWSHandlerFactory : public WebSocketHandlerFactory {
 public:
  ClientWSHandlerFactory(DeviceRegistry* registry,
                         const ServerConfig& server_config);
  std::shared_ptr<WebSocketHandler> Build(struct lws* wsi) override;

 private:
  DeviceRegistry* registry_;
  const ServerConfig& server_config_;
};

class PollConnectionHandler;

class PollConnectionStore {
  public:
    PollConnectionStore() = default;

    std::shared_ptr<PollConnectionHandler> Get(const std::string& conn_id) const;
    std::string Add(std::shared_ptr<PollConnectionHandler> handler);
  private:
   std::map<std::string, std::shared_ptr<PollConnectionHandler>>
       handlers_;
};

class ClientDynHandler : public DynHandler,
                         public std::enable_shared_from_this<ClientHandler> {
 public:
  ClientDynHandler(struct lws* wsi, PollConnectionStore* poll_store);

  HttpStatusCode DoGet() override;
  HttpStatusCode DoPost() override;

 protected:
  virtual HttpStatusCode DoPostInner(std::shared_ptr<PollConnectionHandler>,
                                     const Json::Value&) = 0;
  // In the base class because it's shared by some of the subclasses
  HttpStatusCode Poll(std::shared_ptr<PollConnectionHandler>);

  void Reply(const Json::Value& json);
  void ReplyError(const std::string& message);
  bool ParseInput();

  PollConnectionStore* poll_store_;
};

class ConnectHandler : public ClientDynHandler {
 public:
  ConnectHandler(struct lws* wsi, DeviceRegistry* registry,
                 PollConnectionStore* poll_store);

 protected:
  HttpStatusCode DoPostInner(std::shared_ptr<PollConnectionHandler>,
                             const Json::Value&) override;
 private:
  DeviceRegistry* registry_;
};

class ForwardHandler : public ClientDynHandler {
 public:
  ForwardHandler(struct lws* wsi, PollConnectionStore* poll_store);

 protected:
  HttpStatusCode DoPostInner(std::shared_ptr<PollConnectionHandler>,
                             const Json::Value&) override;
};

class PollHandler : public ClientDynHandler {
 public:
  PollHandler(struct lws* wsi, PollConnectionStore* poll_store);

 protected:
  HttpStatusCode DoPostInner(std::shared_ptr<PollConnectionHandler>,
                             const Json::Value&) override;
};

class ConfigHandler : public DynHandler {
 public:
  ConfigHandler(struct lws* wsi, const ServerConfig& server_config);

  HttpStatusCode DoGet() override;

  HttpStatusCode DoPost() override;

 private:
  const ServerConfig& server_config_;
};

}  // namespace cuttlefish

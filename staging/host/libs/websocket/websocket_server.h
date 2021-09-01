/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <libwebsockets.h>

#include <host/libs/websocket/websocket_handler.h>

namespace cuttlefish {
class WebSocketServer {
 public:
  // Uses HTTP and WS
  WebSocketServer(const char* protocol_name, const std::string& assets_dir,
                  int port);
  // Uses HTTPS and WSS when a certificates directory is provided
  WebSocketServer(const char* protocol_name, const std::string& certs_dir,
                  const std::string& assets_dir, int port);
  ~WebSocketServer() = default;

  // Register a handler factory for websocket connections. A new handler will be
  // created for each new websocket connection.
  void RegisterHandlerFactory(
      const std::string& path,
      std::unique_ptr<WebSocketHandlerFactory> handler_factory_p);

  // Register a handler factory for dynamic HTTP requests. A new handler will be
  // created for each HTTP request.
  void RegisterDynHandlerFactory(const std::string& path,
                                 DynHandlerFactory handler_factory);

  void Serve();

 private:
  static int WebsocketCallback(struct lws* wsi,
                               enum lws_callback_reasons reason, void* user,
                               void* in, size_t len);

  static int DynHttpCallback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);

  int ServerCallback(struct lws* wsi, enum lws_callback_reasons reason,
                            void* user, void* in, size_t len);
  int DynServerCallback(struct lws* wsi,
                               enum lws_callback_reasons reason, void* user,
                               void* in, size_t len);
  std::shared_ptr<WebSocketHandler> InstantiateHandler(
      const std::string& uri_path, struct lws* wsi);
  std::unique_ptr<DynHandler> InstantiateDynHandler(
      const std::string& uri_path, struct lws* wsi);

  void InitializeLwsObjects();

  std::unordered_map<struct lws*, std::shared_ptr<WebSocketHandler>> handlers_ =
      {};
  std::unordered_map<std::string, std::unique_ptr<WebSocketHandlerFactory>>
      handler_factories_ = {};
  std::unordered_map<struct lws*, std::unique_ptr<DynHandler>> dyn_handlers_ =
      {};
  std::unordered_map<std::string, DynHandlerFactory> dyn_handler_factories_ =
      {};
  std::string protocol_name_;
  std::string assets_dir_;
  std::string certs_dir_;
  int server_port_;
  struct lws_context* context_;
  struct lws_http_mount static_mount_;
  std::vector<struct lws_http_mount> dyn_mounts_ = {};
  struct lws_protocol_vhost_options headers_;
  lws_retry_bo_t retry_;
};

}  // namespace cuttlefish

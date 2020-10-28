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

#include <android-base/logging.h>
#include <libwebsockets.h>

#include <host/libs/websocket/websocket_handler.h>

namespace cuttlefish {
class WebSocketServer {
 public:
  WebSocketServer(
    const char* protocol_name,
    const std::string &certs_dir,
    const std::string &assets_dir,
    int port);
  ~WebSocketServer() = default;

  void RegisterHandlerFactory(
    const std::string &path,
    std::unique_ptr<WebSocketHandlerFactory> handler_factory_p);
  void Serve();


 private:
  static std::unordered_map<struct lws*, std::shared_ptr<WebSocketHandler>> handlers_;
  static std::unordered_map<std::string, std::unique_ptr<WebSocketHandlerFactory>>
      handler_factories_;

  static std::string GetPath(struct lws* wsi);
  static int ServerCallback(struct lws* wsi, enum lws_callback_reasons reason,
                            void* user, void* in, size_t len);
  static std::shared_ptr<WebSocketHandler> InstantiateHandler(
      const std::string& uri_path, struct lws* wsi);

  struct lws_context* context_;
  struct lws_http_mount mount_;
  struct lws_protocol_vhost_options headers_;
  lws_retry_bo_t retry_;
};

}  // namespace cuttlefish

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

#include <host/libs/websocket/websocket_server.h>

#include <string>
#include <unordered_map>

#include <android-base/logging.h>
#include <libwebsockets.h>

#include <host/libs/websocket/websocket_handler.h>

namespace cuttlefish {
WebSocketServer::WebSocketServer(
    const char* protocol_name,
    const std::string &certs_dir,
    const std::string &assets_dir,
    int server_port) {
  std::string cert_file = certs_dir + "/server.crt";
  std::string key_file = certs_dir + "/server.key";

  retry_ = {
      .secs_since_valid_ping = 3,
      .secs_since_valid_hangup = 10,
  };

  struct lws_protocols protocols[] = {
      {protocol_name, ServerCallback, 4096, 0, 0, nullptr, 0},
      {nullptr, nullptr, 0, 0, 0, nullptr, 0}};

  mount_ = {
      .mount_next = nullptr,
      .mountpoint = "/",
      .mountpoint_len = 1,
      .origin = assets_dir.c_str(),
      .def = "index.html",
      .protocol = nullptr,
      .cgienv = nullptr,
      .extra_mimetypes = nullptr,
      .interpret = nullptr,
      .cgi_timeout = 0,
      .cache_max_age = 0,
      .auth_mask = 0,
      .cache_reusable = 0,
      .cache_revalidate = 0,
      .cache_intermediaries = 0,
      .origin_protocol = LWSMPRO_FILE,  // files in a dir
      .basic_auth_login_file = nullptr,
  };

  struct lws_context_creation_info info;
  headers_ = {NULL, NULL,
    "content-security-policy:",
      "default-src 'self'; "
      "style-src 'self' https://fonts.googleapis.com/; "
      "font-src  https://fonts.gstatic.com/; "};

  memset(&info, 0, sizeof info);
  info.port = server_port;
  info.mounts = &mount_;
  info.protocols = protocols;
  info.vhost_name = "localhost";
  info.ws_ping_pong_interval = 10;
  info.headers = &headers_;
  info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.ssl_cert_filepath = cert_file.c_str();
  info.ssl_private_key_filepath = key_file.c_str();
  info.retry_and_idle_policy = &retry_;

  context_ = lws_create_context(&info);
  if (!context_) {
    LOG(FATAL) << "Failed to create websocket context";
  }
}

void WebSocketServer::RegisterHandlerFactory(
    const std::string &path,
    std::unique_ptr<WebSocketHandlerFactory> handler_factory_p) {
  handler_factories_[path] = std::move(handler_factory_p);
}

void WebSocketServer::Serve() {
  int n = 0;
  while (n >= 0) {
    n = lws_service(context_, 0);
  }
  lws_context_destroy(context_);
}

std::unordered_map<struct lws*, std::shared_ptr<WebSocketHandler>> WebSocketServer::handlers_ = {};
std::unordered_map<std::string, std::unique_ptr<WebSocketHandlerFactory>>
    WebSocketServer::handler_factories_ = {};

std::string WebSocketServer::GetPath(struct lws* wsi) {
  auto len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);
  std::string path(len + 1, '\0');
  auto ret = lws_hdr_copy(wsi, path.data(), path.size(), WSI_TOKEN_GET_URI);
  if (ret <= 0) {
      len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COLON_PATH);
      path.resize(len + 1, '\0');
      ret = lws_hdr_copy(wsi, path.data(), path.size(), WSI_TOKEN_HTTP_COLON_PATH);
  }
  if (ret < 0) {
    LOG(FATAL) << "Something went wrong getting the path";
  }
  path.resize(len);
  return path;
}

int WebSocketServer::ServerCallback(struct lws* wsi, enum lws_callback_reasons reason,
                                    void* user, void* in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
      auto path = GetPath(wsi);
      auto handler = InstantiateHandler(path, wsi);
      if (!handler) {
        // This message came on an unexpected uri, close the connection.
        lws_close_reason(wsi, LWS_CLOSE_STATUS_NOSTATUS, (uint8_t*)"404", 3);
        return -1;
      }
      handlers_[wsi] = handler;
      handler->OnConnected();
      break;
    }
    case LWS_CALLBACK_CLOSED: {
      auto handler = handlers_[wsi];
      if (handler) {
        handler->OnClosed();
        handlers_.erase(wsi);
      }
      break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
      auto handler = handlers_[wsi];
      if (handler) {
        auto should_close = handler->OnWritable();
        if (should_close) {
          lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, nullptr, 0);
          return 1;
        }
      } else {
        LOG(WARNING) << "Unknown wsi became writable";
        return -1;
      }
      break;
    }
    case LWS_CALLBACK_RECEIVE: {
      auto handler = handlers_[wsi];
      if (handler) {
        bool is_final = (lws_remaining_packet_payload(wsi) == 0) &&
                        lws_is_final_fragment(wsi);
        handler->OnReceive(reinterpret_cast<const uint8_t*>(in), len,
                           lws_frame_is_binary(wsi), is_final);
      } else {
        LOG(WARNING) << "Unkwnown wsi sent data";
      }
      break;
    }
    default:
      return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
  return 0;
}

std::shared_ptr<WebSocketHandler> WebSocketServer::InstantiateHandler(
    const std::string& uri_path, struct lws* wsi) {
  auto it = handler_factories_.find(uri_path);
  if (it == handler_factories_.end()) {
    LOG(ERROR) << "Wrong path provided in URI: " << uri_path;
    return nullptr;
  } else {
    LOG(INFO) << "Creating handler for " << uri_path;
    return it->second->Build(wsi);
  }
}

}  // namespace cuttlefish

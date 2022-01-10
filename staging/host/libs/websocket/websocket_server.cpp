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

#include <common/libs/utils/files.h>
#include <host/libs/websocket/websocket_handler.h>

namespace cuttlefish {
namespace {

std::string GetPath(struct lws* wsi) {
  auto len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);
  std::string path(len + 1, '\0');
  auto ret = lws_hdr_copy(wsi, path.data(), path.size(), WSI_TOKEN_GET_URI);
  if (ret <= 0) {
    len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COLON_PATH);
    path.resize(len + 1, '\0');
    ret =
        lws_hdr_copy(wsi, path.data(), path.size(), WSI_TOKEN_HTTP_COLON_PATH);
  }
  if (ret < 0) {
    LOG(FATAL) << "Something went wrong getting the path";
  }
  path.resize(len);
  return path;
}

const std::vector<std::pair<std::string, std::string>> kCORSHeaders = {
    {"Access-Control-Allow-Origin:", "*"},
    {"Access-Control-Allow-Methods:", "POST, GET, OPTIONS"},
    {"Access-Control-Allow-Headers:",
     "Content-Type, Access-Control-Allow-Headers, Authorization, "
     "X-Requested-With, Accept"}};

bool AddCORSHeaders(struct lws* wsi, unsigned char** buffer_ptr,
                    unsigned char* buffer_end) {
  for (const auto& header : kCORSHeaders) {
    const auto& name = header.first;
    const auto& value = header.second;
    if (lws_add_http_header_by_name(
            wsi, reinterpret_cast<const unsigned char*>(name.c_str()),
            reinterpret_cast<const unsigned char*>(value.c_str()), value.size(),
            buffer_ptr, buffer_end)) {
      return false;
    }
  }
  return true;
}

bool WriteCommonHttpHeaders(int status, const char* mime_type,
                            size_t content_len, struct lws* wsi) {
  constexpr size_t BUFF_SIZE = 2048;
  uint8_t header_buffer[LWS_PRE + BUFF_SIZE];
  const auto start = &header_buffer[LWS_PRE];
  auto p = &header_buffer[LWS_PRE];
  auto end = start + BUFF_SIZE;
  if (lws_add_http_common_headers(wsi, status, mime_type, content_len, &p,
                                  end)) {
    LOG(ERROR) << "Failed to write headers for response";
    return false;
  }
  if (!AddCORSHeaders(wsi, &p, end)) {
    LOG(ERROR) << "Failed to write CORS headers for response";
    return false;
  }
  if (lws_finalize_write_http_header(wsi, start, &p, end)) {
    LOG(ERROR) << "Failed to finalize headers for response";
    return false;
  }
  return true;
}

}  // namespace
WebSocketServer::WebSocketServer(const char* protocol_name,
                                 const std::string& assets_dir, int server_port)
    : WebSocketServer(protocol_name, "", assets_dir, server_port) {}

WebSocketServer::WebSocketServer(const char* protocol_name,
                                 const std::string& certs_dir,
                                 const std::string& assets_dir, int server_port)
    : protocol_name_(protocol_name),
      assets_dir_(assets_dir),
      certs_dir_(certs_dir),
      server_port_(server_port) {}

void WebSocketServer::InitializeLwsObjects() {
  std::string cert_file = certs_dir_ + "/server.crt";
  std::string key_file = certs_dir_ + "/server.key";
  std::string ca_file = certs_dir_ + "/CA.crt";

  retry_ = {
      .secs_since_valid_ping = 3,
      .secs_since_valid_hangup = 10,
  };

  struct lws_protocols protocols[] =  //
      {{
           .name = protocol_name_.c_str(),
           .callback = WebsocketCallback,
           .per_session_data_size = 0,
           .rx_buffer_size = 0,
           .id = 0,
           .user = this,
           .tx_packet_size = 0,
       },
       {
           .name = "__http_polling__",
           .callback = DynHttpCallback,
           .per_session_data_size = 0,
           .rx_buffer_size = 0,
           .id = 0,
           .user = this,
           .tx_packet_size = 0,
       },
       {
           .name = nullptr,
           .callback = nullptr,
           .per_session_data_size = 0,
           .rx_buffer_size = 0,
           .id = 0,
           .user = nullptr,
           .tx_packet_size = 0,
       }};

  dyn_mounts_.reserve(dyn_handler_factories_.size());
  for (auto& handler_entry : dyn_handler_factories_) {
    auto& path = handler_entry.first;
    dyn_mounts_.push_back({
        .mount_next = nullptr,
        .mountpoint = path.c_str(),
        .mountpoint_len = static_cast<uint8_t>(path.size()),
        .origin = "__http_polling__",
        .def = nullptr,
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
        .origin_protocol = LWSMPRO_CALLBACK,  // dynamic
        .basic_auth_login_file = nullptr,
    });
  }
  struct lws_http_mount* next_mount = nullptr;
  // Set up the linked list after all the mounts have been created to ensure
  // pointers are not invalidated.
  for (auto& mount : dyn_mounts_) {
    mount.mount_next = next_mount;
    next_mount = &mount;
  }

  static_mount_ = {
      .mount_next = next_mount,
      .mountpoint = "/",
      .mountpoint_len = 1,
      .origin = assets_dir_.c_str(),
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
  headers_ = {NULL, NULL, "content-security-policy:",
              "default-src 'self' https://ajax.googleapis.com; "
              "style-src 'self' https://fonts.googleapis.com/; "
              "font-src  https://fonts.gstatic.com/; "};

  memset(&info, 0, sizeof info);
  info.port = server_port_;
  info.mounts = &static_mount_;
  info.protocols = protocols;
  info.vhost_name = "localhost";
  info.headers = &headers_;
  info.retry_and_idle_policy = &retry_;

  if (!certs_dir_.empty()) {
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.ssl_cert_filepath = cert_file.c_str();
    info.ssl_private_key_filepath = key_file.c_str();
    if (FileExists(ca_file)) {
      info.ssl_ca_filepath = ca_file.c_str();
    }
  }

  context_ = lws_create_context(&info);
  if (!context_) {
    LOG(FATAL) << "Failed to create websocket context";
  }
}

void WebSocketServer::RegisterHandlerFactory(
    const std::string& path,
    std::unique_ptr<WebSocketHandlerFactory> handler_factory_p) {
  handler_factories_[path] = std::move(handler_factory_p);
}

void WebSocketServer::RegisterDynHandlerFactory(
    const std::string& path,
    DynHandlerFactory handler_factory) {
  dyn_handler_factories_[path] = std::move(handler_factory);
}

void WebSocketServer::Serve() {
  InitializeLwsObjects();
  int n = 0;
  while (n >= 0) {
    n = lws_service(context_, 0);
  }
  lws_context_destroy(context_);
}

int WebSocketServer::WebsocketCallback(struct lws* wsi,
                                       enum lws_callback_reasons reason,
                                       void* user, void* in, size_t len) {
  auto protocol = lws_get_protocol(wsi);
  if (!protocol) {
    // Some callback reasons are always handled by the first protocol, before a
    // wsi struct is even created.
    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
  return reinterpret_cast<WebSocketServer*>(protocol->user)
      ->ServerCallback(wsi, reason, user, in, len);
}

int WebSocketServer::DynHttpCallback(struct lws* wsi,
                                     enum lws_callback_reasons reason,
                                     void* user, void* in, size_t len) {
  auto protocol = lws_get_protocol(wsi);
  if (!protocol) {
    LOG(ERROR) << "No protocol associated with connection";
    return 1;
  }
  return reinterpret_cast<WebSocketServer*>(protocol->user)
      ->DynServerCallback(wsi, reason, user, in, len);
}

int WebSocketServer::DynServerCallback(struct lws* wsi,
                                       enum lws_callback_reasons reason,
                                       void* user, void* in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_HTTP: {
      char* path_raw;
      int path_len;
      auto method = lws_http_get_uri_and_method(wsi, &path_raw, &path_len);
      if (method < 0) {
        return 1;
      }
      std::string path(path_raw, path_len);
      auto handler = InstantiateDynHandler(path, wsi);
      if (!handler) {
        if (!WriteCommonHttpHeaders(static_cast<int>(HttpStatusCode::NotFound),
                                    "application/json", 0, wsi)) {
          return 1;
        }
        return lws_http_transaction_completed(wsi);
      }
      dyn_handlers_[wsi] = std::move(handler);
      switch (method) {
        case LWSHUMETH_GET: {
          auto status = dyn_handlers_[wsi]->DoGet();
          if (!WriteCommonHttpHeaders(static_cast<int>(status),
                                      "application/json",
                                      dyn_handlers_[wsi]->content_len(), wsi)) {
            return 1;
          }
          // Write the response later, when the server is ready
          lws_callback_on_writable(wsi);
          break;
        }
        case LWSHUMETH_POST:
          // Do nothing until the body has been read
          break;
        case LWSHUMETH_OPTIONS: {
          // Response for CORS preflight
          auto status = HttpStatusCode::NoContent;
          if (!WriteCommonHttpHeaders(static_cast<int>(status), "", 0, wsi)) {
            return 1;
          }
          lws_callback_on_writable(wsi);
          break;
        }
        default:
          LOG(ERROR) << "Unsupported HTTP method: " << method;
          return 1;
      }
      break;
    }
    case LWS_CALLBACK_HTTP_BODY: {
      auto handler = dyn_handlers_[wsi].get();
      if (!handler) {
        LOG(WARNING) << "Received body for unknown wsi";
        return 1;
      }
      handler->AppendDataIn(in, len);
      break;
    }
    case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
      auto handler = dyn_handlers_[wsi].get();
      if (!handler) {
        LOG(WARNING) << "Unexpected body completion event from unknown wsi";
        return 1;
      }
      auto status = handler->DoPost();
      if (!WriteCommonHttpHeaders(static_cast<int>(status), "application/json",
                                  dyn_handlers_[wsi]->content_len(), wsi)) {
        return 1;
      }
      lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_HTTP_WRITEABLE: {
      auto handler = dyn_handlers_[wsi].get();
      if (!handler) {
        LOG(WARNING) << "Unknown wsi became writable";
        return 1;
      }
      auto ret = handler->OnWritable();
      dyn_handlers_.erase(wsi);
      // Make sure the connection (in HTTP 1) or stream (in HTTP 2) is closed
      // after the response is written
      return ret;
    }
    case LWS_CALLBACK_CLOSED_HTTP:
      break;
    default:
      return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
  return 0;
}

int WebSocketServer::ServerCallback(struct lws* wsi,
                                    enum lws_callback_reasons reason,
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
        LOG(WARNING) << "Unknown wsi sent data";
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
    LOG(VERBOSE) << "Creating handler for " << uri_path;
    return it->second->Build(wsi);
  }
}

std::unique_ptr<DynHandler> WebSocketServer::InstantiateDynHandler(
    const std::string& uri_path, struct lws* wsi) {
  auto it = dyn_handler_factories_.find(uri_path);
  if (it == dyn_handler_factories_.end()) {
    LOG(ERROR) << "Wrong path provided in URI: " << uri_path;
    return nullptr;
  } else {
    LOG(VERBOSE) << "Creating handler for " << uri_path;
    return it->second(wsi);
  }
}

}  // namespace cuttlefish

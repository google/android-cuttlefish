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

#include <map>
#include <string>

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <libwebsockets.h>

#include "host/frontend/webrtc_operator/client_handler.h"
#include "host/frontend/webrtc_operator/device_handler.h"
#include "host/frontend/webrtc_operator/device_list_handler.h"
#include "host/frontend/webrtc_operator/websocket_handler.h"

#include "host/libs/config/logging.h"

DEFINE_int32(http_server_port, 8443, "The port for the http server.");
DEFINE_bool(use_secure_http, true, "Whether to use HTTPS or HTTP.");
DEFINE_string(assets_dir, "webrtc",
              "Directory with location of webpage assets.");
DEFINE_string(certs_dir, "webrtc/certs", "Directory to certificates.");
DEFINE_string(stun_server, "stun.l.google.com:19302",
              "host:port of STUN server to use for public address resolution");

namespace {

constexpr auto kRegisterDeviceUriPath = "/register_device";
constexpr auto kConnectClientUriPath = "/connect_client";
constexpr auto kListDevicesUriPath = "/list_devices";

std::shared_ptr<cuttlefish::WebSocketHandler> InstantiateHandler(
    const std::string& uri_path, struct lws* wsi) {
  static cuttlefish::DeviceRegistry device_registry;
  static cuttlefish::ServerConfig server_config({FLAGS_stun_server});

  if (uri_path == kRegisterDeviceUriPath) {
    return std::shared_ptr<cuttlefish::WebSocketHandler>(
        new cuttlefish::DeviceHandler(wsi, &device_registry, server_config));
  } else if (uri_path == kConnectClientUriPath) {
    return std::shared_ptr<cuttlefish::WebSocketHandler>(
        new cuttlefish::ClientHandler(wsi, &device_registry, server_config));
  } else if (uri_path == kListDevicesUriPath) {
    return std::shared_ptr<cuttlefish::WebSocketHandler>(
        new cuttlefish::DeviceListHandler(wsi, device_registry));
  } else {
    LOG(ERROR) << "Wrong path provided in URI: " << uri_path;
    return nullptr;
  }
}

// This can only be called from the callback when reason ==
// LWS_CALLBACK_ESTABLISHED
std::string GetPath(struct lws* wsi) {
  auto len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);
  std::string path(len + 1, '\0');
  auto ret = lws_hdr_copy(wsi, path.data(), path.size(), WSI_TOKEN_GET_URI);
  if (ret < 0) {
    LOG(FATAL) << "Something went wrong getting the path";
  }
  path.resize(len);
  return path;
}

int ServerCallback(struct lws* wsi, enum lws_callback_reasons reason,
                   void* user, void* in, size_t len) {
  static std::map<struct lws*, std::shared_ptr<cuttlefish::WebSocketHandler>>
      handlers;

  switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
      auto path = GetPath(wsi);
      auto handler = InstantiateHandler(path, wsi);
      if (!handler) {
        // This message came on an unexpected uri, close the connection.
        lws_close_reason(wsi, LWS_CLOSE_STATUS_NOSTATUS, (uint8_t*)"404", 3);
        return -1;
      }
      handlers[wsi] = handler;
      handler->OnConnected();
      break;
    }
    case LWS_CALLBACK_CLOSED: {
      auto handler = handlers[wsi];
      if (handler) {
        handler->OnClosed();
        handlers.erase(wsi);
      }
      break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
      auto handler = handlers[wsi];
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
      auto handler = handlers[wsi];
      if (handler) {
        handler->OnReceive(reinterpret_cast<const uint8_t*>(in), len,
                           lws_frame_is_binary(wsi));
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

}  // namespace

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string cert_file = FLAGS_certs_dir + "/server.crt";
  std::string key_file = FLAGS_certs_dir + "/server.key";

  const lws_retry_bo_t retry = {
      .secs_since_valid_ping = 3,
      .secs_since_valid_hangup = 10,
  };

  struct lws_protocols protocols[] = {
      {"webrtc-operator", ServerCallback, 4096, 0, 0, nullptr, 0},
      {nullptr, nullptr, 0, 0, 0, nullptr, 0}};

  const struct lws_http_mount mount = {
      .mount_next = nullptr,
      .mountpoint = "/",
      .mountpoint_len = 1,
      .origin = FLAGS_assets_dir.c_str(),
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
  struct lws_context* context;
  struct lws_protocol_vhost_options headers = {NULL, NULL,
    "content-security-policy:",
      "default-src 'self'; "
      "style-src 'self' https://fonts.googleapis.com/; "
      "font-src  https://fonts.gstatic.com/; "};

  memset(&info, 0, sizeof info);
  info.port = FLAGS_http_server_port;
  info.mounts = &mount;
  info.protocols = protocols;
  info.vhost_name = "localhost";
  info.ws_ping_pong_interval = 10;
  info.headers = &headers;
  info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.ssl_cert_filepath = cert_file.c_str();
  info.ssl_private_key_filepath = key_file.c_str();
  info.retry_and_idle_policy = &retry;

  context = lws_create_context(&info);
  if (!context) {
    return -1;
  }

  int n = 0;
  while (n >= 0) {
    n = lws_service(context, 0);
  }
  lws_context_destroy(context);
  return 0;
}

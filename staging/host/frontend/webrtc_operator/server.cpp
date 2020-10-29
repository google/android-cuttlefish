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

#include "host/frontend/webrtc_operator/client_handler.h"
#include "host/frontend/webrtc_operator/device_handler.h"
#include "host/frontend/webrtc_operator/device_list_handler.h"
#include "host/libs/websocket/websocket_handler.h"
#include "host/libs/websocket/websocket_server.h"

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

}  // namespace

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::DeviceRegistry device_registry;
  cuttlefish::ServerConfig server_config({FLAGS_stun_server});

  cuttlefish::WebSocketServer wss(
        "webrtc-operator", FLAGS_certs_dir, FLAGS_assets_dir, FLAGS_http_server_port);

  auto device_handler_factory_p =
      std::unique_ptr<cuttlefish::WebSocketHandlerFactory>(
          new cuttlefish::DeviceHandlerFactory(&device_registry, server_config));
  wss.RegisterHandlerFactory(kRegisterDeviceUriPath, std::move(device_handler_factory_p));
  auto client_handler_factory_p =
      std::unique_ptr<cuttlefish::WebSocketHandlerFactory>(
          new cuttlefish::ClientHandlerFactory(&device_registry, server_config));
  wss.RegisterHandlerFactory(kConnectClientUriPath, std::move(client_handler_factory_p));
  auto device_list_handler_factory_p =
      std::unique_ptr<cuttlefish::WebSocketHandlerFactory>(
          new cuttlefish::DeviceListHandlerFactory(device_registry));
  wss.RegisterHandlerFactory(kListDevicesUriPath, std::move(device_list_handler_factory_p));

  wss.Serve();
  return 0;
}

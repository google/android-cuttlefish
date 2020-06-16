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

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <netdb.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include "host/frontend/gcastv2/https/include/https/HTTPServer.h"
#include "host/frontend/gcastv2/https/include/https/PlainSocket.h"
#include "host/frontend/gcastv2/https/include/https/RunLoop.h"
#include "host/frontend/gcastv2/https/include/https/SSLSocket.h"
#include "host/frontend/gcastv2/https/include/https/SafeCallbackable.h"
#include "host/frontend/gcastv2/https/include/https/Support.h"
#include "host/frontend/gcastv2/webrtc/Utils.h"
#include "host/frontend/gcastv2/webrtc/include/webrtc/STUNClient.h"
#include "host/frontend/gcastv2/webrtc/include/webrtc/STUNMessage.h"

#include "host/frontend/gcastv2/signaling_server/client_handler.h"
#include "host/frontend/gcastv2/signaling_server/device_handler.h"
#include "host/frontend/gcastv2/signaling_server/device_list_handler.h"
#include "host/frontend/gcastv2/signaling_server/device_registry.h"
#include "host/frontend/gcastv2/signaling_server/server_config.h"

#include "host/libs/config/logging.h"

DEFINE_int32(http_server_port, 8443, "The port for the http server.");
DEFINE_bool(use_secure_http, true, "Whether to use HTTPS or HTTP.");
DEFINE_string(assets_dir, "webrtc",
              "Directory with location of webpage assets.");
DEFINE_string(certs_dir, "webrtc/certs", "Directory to certificates.");
DEFINE_string(stun_server, "stun.l.google.com:19302",
              "host:port of STUN server to use for public address resolution");

namespace {

void InitSSL() {
  SSL_library_init();
  SSL_load_error_strings();
}

void ServeStaticFiles(std::shared_ptr<HTTPServer> httpd) {
  const std::string index_html = FLAGS_assets_dir + "/index.html";
  const std::string logcat_js = FLAGS_assets_dir + "/js/logcat.js";
  const std::string app_js = FLAGS_assets_dir + "/js/app.js";
  const std::string viewpane_js = FLAGS_assets_dir + "/js/viewpane.js";
  const std::string cf_webrtc_js = FLAGS_assets_dir + "/js/cf_webrtc.js";
  const std::string style_css = FLAGS_assets_dir + "/style.css";

  httpd->addStaticFile("/index.html", index_html.c_str());
  httpd->addStaticFile("/js/logcat.js", logcat_js.c_str());
  httpd->addStaticFile("/js/app.js", app_js.c_str());
  httpd->addStaticFile("/js/viewpane.js", viewpane_js.c_str());
  httpd->addStaticFile("/js/cf_webrtc.js", cf_webrtc_js.c_str());
  httpd->addStaticFile("/style.css", style_css.c_str());
}

}  // namespace

int main(int argc, char **argv) {
  cvd::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  InitSSL();

  auto run_loop = RunLoop::main();

  auto port = FLAGS_http_server_port;
 /******************************************************************************
  * WARNING!: The device registry doesn't need synchronization because it runs *
  * in this run_loop. If a different run_loop or server implementation is used *
  * synchronization all over needs to be revised.                              *
  *****************************************************************************/

  auto httpd = std::make_shared<HTTPServer>(
      run_loop, "0.0.0.0", port,
      FLAGS_use_secure_http ? ServerSocket::TransportType::TLS
                            : ServerSocket::TransportType::TCP,
      FLAGS_certs_dir + "/server.crt", FLAGS_certs_dir + "/server.key");

  ServeStaticFiles(httpd);

  cvd::ServerConfig server_config({FLAGS_stun_server});
  cvd::DeviceRegistry device_registry;

  httpd->addWebSocketHandlerFactory(
      "/register_device", [&device_registry, &server_config] {
        return std::make_pair(0, std::make_shared<cvd::DeviceHandler>(
                                     &device_registry, server_config));
      });

  httpd->addWebSocketHandlerFactory(
      "/connect_client", [&device_registry, &server_config] {
        return std::make_pair(0, std::make_shared<cvd::ClientHandler>(
                                     &device_registry, server_config));
      });

  // This is non-standard utility endpoint, it's the simplest way for clients to
  // obtain the ids of registered devices.
  httpd->addWebSocketHandlerFactory("/list_devices", [&device_registry] {
    return std::make_pair(
        0, std::make_shared<cvd::DeviceListHandler>(device_registry));
  });

  httpd->run();
  run_loop->run();

  return 0;
}

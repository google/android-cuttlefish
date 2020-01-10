/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <webrtc/AdbWebSocketHandler.h>
#include <webrtc/DTLS.h>
#include <webrtc/MyWebSocketHandler.h>
#include <webrtc/RTPSocketHandler.h>
#include <webrtc/ServerState.h>
#include <webrtc/STUNMessage.h>

#include <https/HTTPServer.h>
#include <https/PlainSocket.h>
#include <https/RunLoop.h>
#include <https/SafeCallbackable.h>
#include <https/SSLSocket.h>
#include <https/Support.h>

#include <iostream>
#include <unordered_map>

#include <gflags/gflags.h>

DEFINE_int32(http_server_port, 8443, "The port for the http server.");
DEFINE_bool(use_secure_http, true, "Whether to use HTTPS or HTTP.");
DEFINE_string(
        public_ip,
        "0.0.0.0",
        "Public IPv4 address of your server, a.b.c.d format");
DEFINE_string(
        assets_dir,
        "webrtc",
        "Directory with location of webpage assets.");
DEFINE_string(
        certs_dir,
        "webrtc/certs",
        "Directory to certificates.");

DEFINE_int32(touch_fd, -1, "An fd to listen on for touch connections.");
DEFINE_int32(keyboard_fd, -1, "An fd to listen on for keyboard connections.");
DEFINE_int32(frame_server_fd, -1, "An fd to listen on for frame updates");
DEFINE_bool(write_virtio_input, false, "Whether to send input events in virtio format.");

DEFINE_string(adb, "", "Interface:port of local adb service.");

int main(int argc, char **argv) {
    ::gflags::ParseCommandLineFlags(&argc, &argv, true);

    SSLSocket::Init();
    DTLS::Init();

    auto runLoop = RunLoop::main();

    auto state = std::make_shared<ServerState>(
            runLoop, ServerState::VideoFormat::VP8);

    auto port = FLAGS_http_server_port;

    auto httpd = std::make_shared<HTTPServer>(
            runLoop,
            "0.0.0.0",
            port,
            FLAGS_use_secure_http
                ? ServerSocket::TransportType::TLS
                : ServerSocket::TransportType::TCP,
            FLAGS_certs_dir + "/server.crt",
            FLAGS_certs_dir + "/server.key");

    const std::string index_html = FLAGS_assets_dir + "/index.html";
    const std::string receive_js = FLAGS_assets_dir + "/js/receive.js";
    const std::string logcat_js = FLAGS_assets_dir + "/js/logcat.js";
    const std::string style_css = FLAGS_assets_dir + "/style.css";

    httpd->addStaticFile("/index.html", index_html.c_str());
    httpd->addStaticFile("/js/receive.js", receive_js.c_str());
    httpd->addStaticFile("/js/logcat.js", logcat_js.c_str());
    httpd->addStaticFile("/style.css", style_css.c_str());

    httpd->addWebSocketHandlerFactory(
            "/control",
            [runLoop, state]{
                auto id = state->acquireHandlerId();

                auto handler =
                    std::make_shared<MyWebSocketHandler>(runLoop, state, id);

                return std::make_pair(0 /* OK */, handler);
            });

    if (!FLAGS_adb.empty()) {
        httpd->addWebSocketHandlerFactory(
                "/control_adb",
                [runLoop]{
                    auto handler = std::make_shared<AdbWebSocketHandler>(
                            runLoop, FLAGS_adb);

                    handler->run();

                    return std::make_pair(0 /* OK */, handler);
                });
    }

    httpd->run();
    runLoop->run();

    return 0;
}

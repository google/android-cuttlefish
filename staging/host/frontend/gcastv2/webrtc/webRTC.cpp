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

#include "Utils.h"

#include <webrtc/AdbWebSocketHandler.h>
#include <webrtc/DTLS.h>
#include <webrtc/MyWebSocketHandler.h>
#include <webrtc/RTPSocketHandler.h>
#include <webrtc/ServerState.h>
#include <webrtc/STUNClient.h>
#include <webrtc/STUNMessage.h>

#include <https/HTTPServer.h>
#include <https/PlainSocket.h>
#include <https/RunLoop.h>
#include <https/SafeCallbackable.h>
#include <https/SSLSocket.h>
#include <https/Support.h>

#include <iostream>
#include <unordered_map>

#include <netdb.h>

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

DEFINE_string(
        stun_server,
        "stun.l.google.com:19302",
        "host:port of STUN server to use for public address resolution");

int main(int argc, char **argv) {
    ::gflags::ParseCommandLineFlags(&argc, &argv, true);

    SSLSocket::Init();
    DTLS::Init();

    if (FLAGS_public_ip.empty() || FLAGS_public_ip == "0.0.0.0") {
        // NOTE: We only contact the external STUN server once upon startup
        // to determine our own public IP.
        // This only works if NAT does not remap ports, i.e. a local port 15550
        // is visible to the outside world on port 15550 as well.
        // If this condition is not met, this code will have to be modified
        // and a STUN request made for each locally bound socket before
        // fulfilling a "MyWebSocketHandler::getCandidate" ICE request.

        const addrinfo kHints = {
            AI_ADDRCONFIG,
            PF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP,
            0,  // ai_addrlen
            nullptr,  // ai_addr
            nullptr,  // ai_canonname
            nullptr  // ai_next
        };

        auto pieces = SplitString(FLAGS_stun_server, ':');
        CHECK_EQ(pieces.size(), 2u);

        addrinfo *infos;
        CHECK(!getaddrinfo(pieces[0].c_str(), pieces[1].c_str(), &kHints, &infos));

        sockaddr_storage stunAddr;
        memcpy(&stunAddr, infos->ai_addr, infos->ai_addrlen);

        freeaddrinfo(infos);
        infos = nullptr;

        CHECK_EQ(stunAddr.ss_family, AF_INET);

        std::mutex lock;
        std::condition_variable cond;
        bool done = false;

        auto runLoop = std::make_shared<RunLoop>("STUN");

        auto stunClient = std::make_shared<STUNClient>(
                runLoop,
                reinterpret_cast<const sockaddr_in &>(stunAddr),
                [&lock, &cond, &done](int result, const std::string &myPublicIp) {
                    CHECK(!result);
                    LOG(INFO)
                        << "STUN-discovered public IP: " << myPublicIp;

                    FLAGS_public_ip = myPublicIp;

                    std::lock_guard autoLock(lock);
                    done = true;
                    cond.notify_all();
                });

        stunClient->run();

        std::unique_lock autoLock(lock);
        while (!done) {
            cond.wait(autoLock);
        }
    }

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

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

#include <android-base/logging.h>
#include <android-base/strings.h>

#include <host/libs/config/cuttlefish_config.h>
#include <host/libs/config/logging.h>

#include "Utils.h"

#include <webrtc/AdbHandler.h>
#include <webrtc/DTLS.h>
#include <webrtc/RTPSocketHandler.h>
#include <webrtc/ServerState.h>
#include <webrtc/sig_server_handler.h>

#include <https/HTTPServer.h>
#include <https/PlainSocket.h>
#include <https/RunLoop.h>
#include <https/SSLSocket.h>
#include <https/SafeCallbackable.h>
#include <https/Support.h>

#include <iostream>
#include <unordered_map>

#include <netdb.h>

#include <gflags/gflags.h>

DEFINE_string(public_ip, "0.0.0.0", "Public IPv4 address, a.b.c.d format");

DEFINE_int32(touch_fd, -1, "An fd to listen on for touch connections.");
DEFINE_int32(keyboard_fd, -1, "An fd to listen on for keyboard connections.");
DEFINE_int32(frame_server_fd, -1, "An fd to listen on for frame updates");
DEFINE_bool(write_virtio_input, false,
            "Whether to send input events in virtio format.");

DEFINE_string(adb, "", "Interface:port of local adb service.");

int main(int argc, char **argv) {
  cvd::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  SSLSocket::Init();
  DTLS::Init();

  auto config = vsoc::CuttlefishConfig::Get();

  auto sig_server_addr = config->sig_server_address();
  auto sig_server_port = config->sig_server_port();
  auto sig_server_path = config->sig_server_path();
  auto sig_server_strict = config->sig_server_strict();
  auto device_id = config->ForDefaultInstance().webrtc_device_id();

  auto runLoop = RunLoop::main();

  auto state =
      std::make_shared<ServerState>(runLoop, ServerState::VideoFormat::VP8);

  auto security = WsConnection::Security::kAllowSelfSigned;
  if (sig_server_strict) {
    security = WsConnection::Security::kStrict;
  }

  auto sig_server_handler =
      std::make_shared<SigServerHandler>(device_id, state);

  sig_server_handler->Connect(sig_server_addr, sig_server_port, sig_server_path,
                              security);

  runLoop->run();

  return 0;
}

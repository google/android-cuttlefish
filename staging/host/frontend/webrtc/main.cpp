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

#include <linux/input.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <libyuv.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "host/frontend/webrtc/connection_observer.h"
#include "host/frontend/webrtc/display_handler.h"
#include "host/frontend/webrtc/lib/streamer.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"
#include "host/libs/screen_connector/screen_connector.h"

DEFINE_int32(touch_fd, -1, "An fd to listen on for touch connections.");
DEFINE_int32(keyboard_fd, -1, "An fd to listen on for keyboard connections.");
DEFINE_int32(frame_server_fd, -1, "An fd to listen on for frame updates");
DEFINE_int32(kernel_log_events_fd, -1, "An fd to listen on for kernel log events.");
DEFINE_bool(write_virtio_input, false,
            "Whether to send input events in virtio format.");

using cuttlefish::CfConnectionObserverFactory;
using cuttlefish::DisplayHandler;
using cuttlefish::webrtc_streaming::Streamer;
using cuttlefish::webrtc_streaming::StreamerConfig;

class CfOperatorObserver : public cuttlefish::webrtc_streaming::OperatorObserver {
 public:
  virtual ~CfOperatorObserver() = default;
  virtual void OnRegistered() override {
    LOG(VERBOSE) << "Registered with Operator";
  }
  virtual void OnClose() override {
    LOG(FATAL) << "Connection with Operator unexpectedly closed";
  }
  virtual void OnError() override {
    LOG(FATAL) << "Error encountered in connection with Operator";
  }
};

static std::vector<std::pair<std::string, std::string>> ParseHttpHeaders(
    const std::string& path) {
  auto fd = cuttlefish::SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen()) {
    LOG(WARNING) << "Unable to open operator (signaling server) headers file, "
                    "connecting to the operator will probably fail: "
                 << fd->StrError();
    return {};
  }
  std::string raw_headers;
  auto res = cuttlefish::ReadAll(fd, &raw_headers);
  if (res < 0) {
    LOG(WARNING) << "Unable to open operator (signaling server) headers file, "
                    "connecting to the operator will probably fail: "
                 << fd->StrError();
    return {};
  }
  std::vector<std::pair<std::string, std::string>> headers;
  std::size_t raw_index = 0;
  while (raw_index < raw_headers.size()) {
    auto colon_pos = raw_headers.find(':', raw_index);
    if (colon_pos == std::string::npos) {
      LOG(ERROR)
          << "Expected to find ':' in each line of the operator headers file";
      break;
    }
    auto eol_pos = raw_headers.find('\n', colon_pos);
    if (eol_pos == std::string::npos) {
      eol_pos = raw_headers.size();
    }
    // If the file uses \r\n as line delimiters exclude the \r too.
    auto eov_pos = raw_headers[eol_pos - 1] == '\r'? eol_pos - 1: eol_pos;
    headers.emplace_back(
        raw_headers.substr(raw_index, colon_pos + 1 - raw_index),
        raw_headers.substr(colon_pos + 1, eov_pos - colon_pos - 1));
    raw_index = eol_pos + 1;
  }
  return headers;
}

int main(int argc, char **argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::InputSockets input_sockets;

  input_sockets.touch_server = cuttlefish::SharedFD::Dup(FLAGS_touch_fd);
  input_sockets.keyboard_server = cuttlefish::SharedFD::Dup(FLAGS_keyboard_fd);
  close(FLAGS_touch_fd);
  close(FLAGS_keyboard_fd);
  // Accepting on these sockets here means the device won't register with the
  // operator as soon as it could, but rather wait until crosvm's input display
  // devices have been initialized. That's OK though, because without those
  // devices there is no meaningful interaction the user can have with the
  // device.
  input_sockets.touch_client =
      cuttlefish::SharedFD::Accept(*input_sockets.touch_server);
  input_sockets.keyboard_client =
      cuttlefish::SharedFD::Accept(*input_sockets.keyboard_server);

  std::thread touch_accepter([&input_sockets](){
    for (;;) {
      input_sockets.touch_client =
          cuttlefish::SharedFD::Accept(*input_sockets.touch_server);
    }
  });
  std::thread keyboard_accepter([&input_sockets](){
    for (;;) {
      input_sockets.keyboard_client =
          cuttlefish::SharedFD::Accept(*input_sockets.keyboard_server);
    }
  });

  auto kernel_log_events_client = cuttlefish::SharedFD::Dup(FLAGS_kernel_log_events_fd);
  close(FLAGS_kernel_log_events_fd);

  auto cvd_config = cuttlefish::CuttlefishConfig::Get();
  auto screen_connector = cuttlefish::ScreenConnector::Get(FLAGS_frame_server_fd);

  StreamerConfig streamer_config;

  streamer_config.device_id =
      cvd_config->ForDefaultInstance().webrtc_device_id();
  streamer_config.tcp_port_range = cvd_config->webrtc_tcp_port_range();
  streamer_config.udp_port_range = cvd_config->webrtc_udp_port_range();
  streamer_config.operator_server.addr = cvd_config->sig_server_address();
  streamer_config.operator_server.port = cvd_config->sig_server_port();
  streamer_config.operator_server.path = cvd_config->sig_server_path();
  streamer_config.operator_server.security =
      cvd_config->sig_server_strict()
          ? WsConnection::Security::kStrict
          : WsConnection::Security::kAllowSelfSigned;

  if (!cvd_config->sig_server_headers_path().empty()) {
    streamer_config.operator_server.http_headers =
        ParseHttpHeaders(cvd_config->sig_server_headers_path());
  }

  auto observer_factory = std::make_shared<CfConnectionObserverFactory>(
      input_sockets, kernel_log_events_client);

  auto streamer = Streamer::Create(streamer_config, observer_factory);
  CHECK(streamer) << "Could not create streamer";

  auto display_0 = streamer->AddDisplay(
      "display_0", screen_connector->ScreenWidth(),
      screen_connector->ScreenHeight(), cvd_config->dpi(), true);
  auto display_handler =
      std::make_shared<DisplayHandler>(display_0, screen_connector);

  observer_factory->SetDisplayHandler(display_handler);

  streamer->SetHardwareSpecs(cvd_config->cpus(), cvd_config->memory_mb());

  std::shared_ptr<cuttlefish::webrtc_streaming::OperatorObserver> operator_observer(
      new CfOperatorObserver());
  streamer->Register(operator_observer);

  display_handler->Loop();

  return 0;
}

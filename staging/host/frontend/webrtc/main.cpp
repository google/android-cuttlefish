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

#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <libyuv.h>

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

int main(int argc, char **argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto touch_server = cuttlefish::SharedFD::Dup(FLAGS_touch_fd);
  auto keyboard_server = cuttlefish::SharedFD::Dup(FLAGS_keyboard_fd);
  close(FLAGS_touch_fd);
  close(FLAGS_keyboard_fd);
  // Accepting on these sockets here means the device won't register with the
  // operator as soon as it could, but rather wait until crosvm's input display
  // devices have been initialized. That's OK though, because without those
  // devices there is no meaningful interaction the user can have with the
  // device.
  auto touch_client = cuttlefish::SharedFD::Accept(*touch_server);
  auto keyboard_client = cuttlefish::SharedFD::Accept(*keyboard_server);

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

  auto observer_factory = std::make_shared<CfConnectionObserverFactory>(
      touch_client, keyboard_client);

  auto streamer = Streamer::Create(streamer_config, observer_factory);

  auto display_0 = streamer->AddDisplay(
      "display_0", screen_connector->ScreenWidth(),
      screen_connector->ScreenHeight(), cvd_config->dpi(), true);
  auto display_handler =
      std::make_shared<DisplayHandler>(display_0, screen_connector);

  observer_factory->SetDisplayHandler(display_handler);

  std::shared_ptr<cuttlefish::webrtc_streaming::OperatorObserver> operator_observer(
      new CfOperatorObserver());
  streamer->Register(operator_observer);

  display_handler->Loop();

  return 0;
}

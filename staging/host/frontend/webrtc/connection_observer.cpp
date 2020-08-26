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

#include "host/frontend/webrtc/connection_observer.h"

#include <linux/input.h>

#include <thread>
#include <vector>

#include <json/json.h>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "host/frontend/webrtc/adb_handler.h"
#include "host/frontend/webrtc/lib/utils.h"
#include "host/libs/config/cuttlefish_config.h"

DECLARE_bool(write_virtio_input);

namespace cuttlefish {

class ConnectionObserverImpl
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  ConnectionObserverImpl(std::shared_ptr<TouchConnector> touch_connector,
                         std::shared_ptr<KeyboardConnector> keyboard_connector,
                         std::weak_ptr<DisplayHandler> display_handler)
      : touch_connector_(touch_connector),
        keyboard_connector_(keyboard_connector),
        weak_display_handler_(display_handler) {}
  virtual ~ConnectionObserverImpl() = default;

  void OnConnected(std::function<void(const uint8_t *, size_t, bool)>
                   /*ctrl_msg_sender*/) override {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      // A long time may pass before the next frame comes up from the guest.
      // Send the last one to avoid showing a black screen to the user during
      // that time.
      display_handler->SendLastFrame();
    }
  }
  void OnTouchEvent(const std::string & /*display_label*/, int x, int y,
                    bool down) override {
    touch_connector_->InjectTouchEvent(x, y, down);
  }
  void OnMultiTouchEvent(const std::string &display_label, int /*id*/,
                         int /*slot*/, int x, int y,
                         bool initialDown) override {
    OnTouchEvent(display_label, x, y, initialDown);
  }
  void OnKeyboardEvent(uint16_t code, bool down) override {
    keyboard_connector_->InjectKeyEvent(code, down);
  }
  void OnAdbChannelOpen(std::function<bool(const uint8_t *, size_t)>
                            adb_message_sender) override {
    LOG(VERBOSE) << "Adb Channel open";
    adb_handler_.reset(new cuttlefish::webrtc_streaming::AdbHandler(
        cuttlefish::CuttlefishConfig::Get()
            ->ForDefaultInstance()
            .adb_ip_and_port(),
        adb_message_sender));
  }
  void OnAdbMessage(const uint8_t *msg, size_t size) override {
    adb_handler_->handleMessage(msg, size);
  }
  void OnControlMessage(const uint8_t* msg, size_t size) override {
    Json::Value evt;
    Json::Reader json_reader;
    if (!json_reader.parse(reinterpret_cast<const char*>(msg),
                           reinterpret_cast<const char*>(msg + size),
                           evt) < 0) {
      LOG(ERROR) << "Received invalid JSON object over control channel";
      return;
    }
    auto result =
        webrtc_streaming::ValidationResult::ValidateJsonObject(evt, "command",
                           {{"command", Json::ValueType::stringValue},
                            {"state", Json::ValueType::stringValue}});
    if (!result.ok()) {
      LOG(ERROR) << result.error();
      return;
    }
    auto command = evt["command"].asString();
    auto state = evt["state"].asString();

    LOG(VERBOSE) << "Control command: " << command << " (" << state << ")";
    if (command == "power") {
      OnKeyboardEvent(KEY_POWER, state == "down");
    }
    else {
      LOG(WARNING) << "Unsupported control command: " << command << " (" << state << ")";
      // TODO(b/163628929): Handle VOLUME commands.
      // TODO(b/163081337): Handle custom commands.
    }
  }

 private:
  std::shared_ptr<TouchConnector> touch_connector_;
  std::shared_ptr<KeyboardConnector> keyboard_connector_;
  std::shared_ptr<cuttlefish::webrtc_streaming::AdbHandler> adb_handler_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
};

CfConnectionObserverFactory::CfConnectionObserverFactory(
    std::shared_ptr<TouchConnector> touch_connector,
    std::shared_ptr<KeyboardConnector> keyboard_connector)
    : touch_connector_(touch_connector),
      keyboard_connector_(keyboard_connector) {}

std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>
CfConnectionObserverFactory::CreateObserver() {
  return std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>(
      new ConnectionObserverImpl(touch_connector_, keyboard_connector_,
                                 weak_display_handler_));
}

void CfConnectionObserverFactory::SetDisplayHandler(
    std::weak_ptr<DisplayHandler> display_handler) {
  weak_display_handler_ = display_handler;
}
}  // namespace cuttlefish

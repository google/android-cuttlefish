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

#define LOG_TAG "ConnectionObserver"

#include "host/frontend/webrtc/connection_observer.h"

#include <linux/input.h>

#include <chrono>
#include <map>
#include <thread>
#include <vector>

#include <json/json.h>

#include <android-base/logging.h>
#include <android-base/parsedouble.h>
#include <gflags/gflags.h>

#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_buf.h"
#include "host/frontend/webrtc/adb_handler.h"
#include "host/frontend/webrtc/bluetooth_handler.h"
#include "host/frontend/webrtc/gpx_locations_handler.h"
#include "host/frontend/webrtc/kml_locations_handler.h"
#include "host/frontend/webrtc/libdevice/camera_controller.h"
#include "host/frontend/webrtc/libdevice/lights_observer.h"
#include "host/frontend/webrtc/location_handler.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/input_connector/input_connector.h"

namespace cuttlefish {

/**
 * connection observer implementation for regular android mode.
 * i.e. when it is not in the confirmation UI mode (or TEE),
 * the control flow will fall back to this ConnectionObserverForAndroid
 */
class ConnectionObserverImpl : public webrtc_streaming::ConnectionObserver {
 public:
  ConnectionObserverImpl(
      std::unique_ptr<InputConnector::EventSink> input_events_sink,
      KernelLogEventsHandler *kernel_log_events_handler,
      std::map<std::string, SharedFD> commands_to_custom_action_servers,
      std::weak_ptr<DisplayHandler> display_handler,
      CameraController *camera_controller,
      std::shared_ptr<webrtc_streaming::SensorsHandler> sensors_handler,
      std::shared_ptr<webrtc_streaming::LightsObserver> lights_observer)
      : input_events_sink_(std::move(input_events_sink)),
        kernel_log_events_handler_(kernel_log_events_handler),
        commands_to_custom_action_servers_(commands_to_custom_action_servers),
        weak_display_handler_(display_handler),
        camera_controller_(camera_controller),
        sensors_handler_(sensors_handler),
        lights_observer_(lights_observer) {}
  virtual ~ConnectionObserverImpl() {
    auto display_handler = weak_display_handler_.lock();
    if (kernel_log_subscription_id_ != -1) {
      kernel_log_events_handler_->Unsubscribe(kernel_log_subscription_id_);
    }
  }

  void OnConnected() override {
    SendLastFrameAsync(/*all displays*/ std::nullopt);
  }

  void OnTouchEvent(const std::string &device_label, int x, int y,
                    bool down) override {
    input_events_sink_->SendTouchEvent(device_label, x, y, down);
  }

  void OnMultiTouchEvent(const std::string &device_label, Json::Value id,
                         Json::Value slot, Json::Value x, Json::Value y,
                         bool down, int size) {
    std::vector<MultitouchSlot> slots(size);
    for (int i = 0; i < size; i++) {
      slots[i].id = id[i].asInt();
      slots[i].x = x[i].asInt();
      slots[i].y = y[i].asInt();
    }
    input_events_sink_->SendMultiTouchEvent(device_label, slots, down);
  }

  void OnKeyboardEvent(uint16_t code, bool down) override {
    input_events_sink_->SendKeyboardEvent(code, down);
  }

  void OnWheelEvent(int pixels) {
    input_events_sink_->SendRotaryEvent(pixels);
  }

  void OnSwitchEvent(uint16_t code, bool state) {
    input_events_sink_->SendSwitchesEvent(code, state);
  }

  void OnAdbChannelOpen(std::function<bool(const uint8_t *, size_t)>
                            adb_message_sender) override {
    LOG(VERBOSE) << "Adb Channel open";
    adb_handler_.reset(new webrtc_streaming::AdbHandler(
        CuttlefishConfig::Get()->ForDefaultInstance().adb_ip_and_port(),
        adb_message_sender));
  }
  void OnAdbMessage(const uint8_t *msg, size_t size) override {
    adb_handler_->handleMessage(msg, size);
  }
  void OnControlChannelOpen(
      std::function<bool(const Json::Value)> control_message_sender) override {
    LOG(VERBOSE) << "Control Channel open";
    if (camera_controller_) {
      camera_controller_->SetMessageSender(control_message_sender);
    }
    kernel_log_subscription_id_ =
        kernel_log_events_handler_->AddSubscriber(control_message_sender);
  }

  void OnLidStateChange(bool lid_open) override {
    // InputManagerService treats a value of 0 as open and 1 as closed, so
    // invert the lid_switch_open value that is sent to the input device.
    OnSwitchEvent(SW_LID, !lid_open);
  }
  void OnHingeAngleChange(int /*hinge_angle*/) override {
    // TODO(b/181157794) Propagate hinge angle sensor data using a custom
    // Sensor HAL.
  }
  void OnPowerButton(bool button_down) override {
    OnKeyboardEvent(KEY_POWER, button_down);
  }
  void OnBackButton(bool button_down) override {
    OnKeyboardEvent(KEY_BACK, button_down);
  }
  void OnHomeButton(bool button_down) override {
    OnKeyboardEvent(KEY_HOMEPAGE, button_down);
  }
  void OnMenuButton(bool button_down) override {
    OnKeyboardEvent(KEY_MENU, button_down);
  }
  void OnVolumeDownButton(bool button_down) override {
    OnKeyboardEvent(KEY_VOLUMEDOWN, button_down);
  }
  void OnVolumeUpButton(bool button_down) override {
    OnKeyboardEvent(KEY_VOLUMEUP, button_down);
  }
  void OnCustomActionButton(const std::string &command,
                            const std::string &button_state) override {
    if (commands_to_custom_action_servers_.find(command) !=
        commands_to_custom_action_servers_.end()) {
      // Simple protocol for commands forwarded to action servers:
      //   - Always 128 bytes
      //   - Format:   command:button_state
      //   - Example:  my_button:down
      std::string action_server_message = command + ":" + button_state;
      WriteAll(commands_to_custom_action_servers_[command],
               action_server_message.c_str(), 128);
    } else {
      LOG(WARNING) << "Unsupported control command: " << command << " ("
                   << button_state << ")";
    }
  }

  void OnBluetoothChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                  bluetooth_message_sender) override {
    LOG(VERBOSE) << "Bluetooth channel open";
    auto config = CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    bluetooth_handler_.reset(new webrtc_streaming::BluetoothHandler(
        config->rootcanal_test_port(), bluetooth_message_sender));
  }

  void OnBluetoothMessage(const uint8_t *msg, size_t size) override {
    bluetooth_handler_->handleMessage(msg, size);
  }

  void OnSensorsChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                sensors_message_sender) override {
    sensors_subscription_id = sensors_handler_->Subscribe(sensors_message_sender);
    LOG(VERBOSE) << "Sensors channel open";
  }

  void OnSensorsChannelClosed() override {
    sensors_handler_->UnSubscribe(sensors_subscription_id);
  }

  void OnSensorsMessage(const uint8_t *msg, size_t size) override {
    std::string msgstr(msg, msg + size);
    std::vector<std::string> xyz = android::base::Split(msgstr, " ");

    if (xyz.size() != 3) {
      LOG(WARNING) << "Invalid rotation angles: Expected 3, received " << xyz.size();
      return;
    }

    double x, y, z;
    CHECK(android::base::ParseDouble(xyz.at(0), &x))
        << "X rotation value must be a double";
    CHECK(android::base::ParseDouble(xyz.at(1), &y))
        << "Y rotation value must be a double";
    CHECK(android::base::ParseDouble(xyz.at(2), &z))
        << "Z rotation value must be a double";
    sensors_handler_->HandleMessage(x, y, z);
  }

  void OnLightsChannelOpen(
      std::function<bool(const Json::Value &)> lights_message_sender) override {
    LOG(DEBUG) << "Lights channel open";

    lights_subscription_id_ =
        lights_observer_->Subscribe(lights_message_sender);
  }

  void OnLightsChannelClosed() override {
    lights_observer_->Unsubscribe(lights_subscription_id_);
  }

  void OnLocationChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                 location_message_sender) override {
    LOG(VERBOSE) << "Location channel open";
    auto config = CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    location_handler_.reset(
        new webrtc_streaming::LocationHandler(location_message_sender));
  }
  void OnLocationMessage(const uint8_t *msg, size_t size) override {
    std::string msgstr(msg, msg + size);

    std::vector<std::string> inputs = android::base::Split(msgstr, ",");

    if (inputs.size() != 3) {
      LOG(WARNING) << "Invalid location length , length = " << inputs.size();
      return;
    }

    float longitude = std::stod(inputs.at(0));
    float latitude = std::stod(inputs.at(1));
    float elevation = std::stod(inputs.at(2));
    location_handler_->HandleMessage(longitude, latitude, elevation);
  }

  void OnKmlLocationsChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                     kml_locations_message_sender) override {
    LOG(VERBOSE) << "Kml Locations channel open";
    auto config = CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    kml_locations_handler_.reset(new webrtc_streaming::KmlLocationsHandler(
        kml_locations_message_sender));
  }
  void OnKmlLocationsMessage(const uint8_t *msg, size_t size) override {
    kml_locations_handler_->HandleMessage(msg, size);
  }

  void OnGpxLocationsChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                     gpx_locations_message_sender) override {
    LOG(VERBOSE) << "Gpx Locations channel open";
    auto config = CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    gpx_locations_handler_.reset(new webrtc_streaming::GpxLocationsHandler(
        gpx_locations_message_sender));
  }
  void OnGpxLocationsMessage(const uint8_t *msg, size_t size) override {
    gpx_locations_handler_->HandleMessage(msg, size);
  }

  void OnCameraControlMsg(const Json::Value &msg) override {
    if (camera_controller_) {
      camera_controller_->HandleMessage(msg);
    } else {
      LOG(VERBOSE) << "Camera control message received but no camera "
                      "controller is available";
    }
  }

  void OnDisplayControlMsg(const Json::Value &msg) override {
    static constexpr const char kRefreshDisplay[] = "refresh_display";
    if (!msg.isMember(kRefreshDisplay)) {
      LOG(ERROR) << "Unknown display control command.";
      return;
    }
    const auto display_number_json = msg[kRefreshDisplay];
    if (!display_number_json.isInt()) {
      LOG(ERROR) << "Invalid display control command.";
      return;
    }
    const auto display_number =
        static_cast<uint32_t>(display_number_json.asInt());
    LOG(VERBOSE) << "Refresh display " << display_number;
    SendLastFrameAsync(display_number);
  }

  void OnCameraData(const std::vector<char> &data) override {
    if (camera_controller_) {
      camera_controller_->HandleMessage(data);
    } else {
      LOG(VERBOSE)
          << "Camera data received but no camera controller is available";
    }
  }

 private:
  void SendLastFrameAsync(std::optional<uint32_t> display_number) {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      std::thread th([this, display_number]() {
        // The encoder in libwebrtc won't drop 5 consecutive frames due to frame
        // size, so we make sure at least 5 frames are sent every time a client
        // connects to ensure they receive at least one.
        constexpr int kNumFrames = 5;
        constexpr int kMillisPerFrame = 16;
        for (int i = 0; i < kNumFrames; ++i) {
          auto display_handler = weak_display_handler_.lock();
          display_handler->SendLastFrame(display_number);

          if (i < kNumFrames - 1) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kMillisPerFrame));
          }
        }
      });
      th.detach();
    }
  }

  std::unique_ptr<InputConnector::EventSink> input_events_sink_;
  KernelLogEventsHandler *kernel_log_events_handler_;
  int kernel_log_subscription_id_ = -1;
  std::shared_ptr<webrtc_streaming::AdbHandler> adb_handler_;
  std::shared_ptr<webrtc_streaming::BluetoothHandler> bluetooth_handler_;
  std::shared_ptr<webrtc_streaming::LocationHandler> location_handler_;
  std::shared_ptr<webrtc_streaming::KmlLocationsHandler> kml_locations_handler_;
  std::shared_ptr<webrtc_streaming::GpxLocationsHandler> gpx_locations_handler_;
  std::map<std::string, SharedFD> commands_to_custom_action_servers_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
  CameraController *camera_controller_;
  std::shared_ptr<webrtc_streaming::SensorsHandler> sensors_handler_;
  std::shared_ptr<webrtc_streaming::LightsObserver> lights_observer_;
  int sensors_subscription_id = -1;
  int lights_subscription_id_ = -1;
};

CfConnectionObserverFactory::CfConnectionObserverFactory(
    InputConnector &input_connector,
    KernelLogEventsHandler *kernel_log_events_handler,
    std::shared_ptr<webrtc_streaming::LightsObserver> lights_observer)
    : input_connector_(input_connector),
      kernel_log_events_handler_(kernel_log_events_handler),
      lights_observer_(lights_observer) {}

std::shared_ptr<webrtc_streaming::ConnectionObserver>
CfConnectionObserverFactory::CreateObserver() {
  return std::shared_ptr<webrtc_streaming::ConnectionObserver>(
      new ConnectionObserverImpl(
          input_connector_.CreateSink(), kernel_log_events_handler_,
          commands_to_custom_action_servers_, weak_display_handler_,
          camera_controller_, shared_sensors_handler_, lights_observer_));
}

void CfConnectionObserverFactory::AddCustomActionServer(
    SharedFD custom_action_server_fd,
    const std::vector<std::string> &commands) {
  for (const std::string &command : commands) {
    LOG(DEBUG) << "Action server is listening to command: " << command;
    commands_to_custom_action_servers_[command] = custom_action_server_fd;
  }
}

void CfConnectionObserverFactory::SetDisplayHandler(
    std::weak_ptr<DisplayHandler> display_handler) {
  weak_display_handler_ = display_handler;
}

void CfConnectionObserverFactory::SetCameraHandler(
    CameraController *controller) {
  camera_controller_ = controller;
}
}  // namespace cuttlefish

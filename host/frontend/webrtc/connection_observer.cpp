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
#include <set>
#include <thread>
#include <vector>

#include <json/json.h>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_buf.h"
#include "host/frontend/webrtc/adb_handler.h"
#include "host/frontend/webrtc/bluetooth_handler.h"
#include "host/frontend/webrtc/gpx_locations_handler.h"
#include "host/frontend/webrtc/kml_locations_handler.h"
#include "host/frontend/webrtc/libdevice/camera_controller.h"
#include "host/frontend/webrtc/location_handler.h"
#include "host/libs/config/cuttlefish_config.h"

DECLARE_bool(write_virtio_input);

namespace cuttlefish {

// TODO (b/147511234): de-dup this from vnc server and here
struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  int32_t value;
};

struct multitouch_slot {
  int32_t id;
  int32_t slot;
  int32_t x;
  int32_t y;
};

struct InputEventBuffer {
  virtual ~InputEventBuffer() = default;
  virtual void AddEvent(uint16_t type, uint16_t code, int32_t value) = 0;
  virtual size_t size() const = 0;
  virtual const void *data() const = 0;
};

template <typename T>
struct InputEventBufferImpl : public InputEventBuffer {
  InputEventBufferImpl() {
    buffer_.reserve(6);  // 6 is usually enough
  }
  void AddEvent(uint16_t type, uint16_t code, int32_t value) override {
    buffer_.push_back({.type = type, .code = code, .value = value});
  }
  T *data() { return buffer_.data(); }
  const void *data() const override { return buffer_.data(); }
  std::size_t size() const override { return buffer_.size() * sizeof(T); }

 private:
  std::vector<T> buffer_;
};

// TODO: we could add an arg here to specify whether we want the multitouch buffer?
std::unique_ptr<InputEventBuffer> GetEventBuffer() {
  if (FLAGS_write_virtio_input) {
    return std::unique_ptr<InputEventBuffer>(
        new InputEventBufferImpl<virtio_input_event>());
  } else {
    return std::unique_ptr<InputEventBuffer>(
        new InputEventBufferImpl<input_event>());
  }
}

/**
 * connection observer implementation for regular android mode.
 * i.e. when it is not in the confirmation UI mode (or TEE),
 * the control flow will fall back to this ConnectionObserverForAndroid
 */
class ConnectionObserverImpl
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  ConnectionObserverImpl(
      cuttlefish::InputSockets &input_sockets,
      cuttlefish::KernelLogEventsHandler *kernel_log_events_handler,
      std::map<std::string, cuttlefish::SharedFD>
          commands_to_custom_action_servers,
      std::weak_ptr<DisplayHandler> display_handler,
      CameraController *camera_controller,
      cuttlefish::confui::HostVirtualInput &confui_input)
      : input_sockets_(input_sockets),
        kernel_log_events_handler_(kernel_log_events_handler),
        commands_to_custom_action_servers_(commands_to_custom_action_servers),
        weak_display_handler_(display_handler),
        camera_controller_(camera_controller),
        confui_input_(confui_input) {}
  virtual ~ConnectionObserverImpl() {
    auto display_handler = weak_display_handler_.lock();
    if (kernel_log_subscription_id_ != -1) {
      kernel_log_events_handler_->Unsubscribe(kernel_log_subscription_id_);
    }
  }

  void OnConnected() override {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      std::thread th([this]() {
        // The encoder in libwebrtc won't drop 5 consecutive frames due to frame
        // size, so we make sure at least 5 frames are sent every time a client
        // connects to ensure they receive at least one.
        constexpr int kNumFrames = 5;
        constexpr int kMillisPerFrame = 16;
        for (int i = 0; i < kNumFrames; ++i) {
          auto display_handler = weak_display_handler_.lock();
          display_handler->SendLastFrame();
          if (i < kNumFrames - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kMillisPerFrame));
          }
        }
      });
      th.detach();
    }
  }

  void OnTouchEvent(const std::string &display_label, int x, int y,
                    bool down) override {
    if (confui_input_.IsConfUiActive()) {
      if (down) {
        confui_input_.TouchEvent(x, y, down);
      }
      return;
    }
    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }
    buffer->AddEvent(EV_ABS, ABS_X, x);
    buffer->AddEvent(EV_ABS, ABS_Y, y);
    buffer->AddEvent(EV_KEY, BTN_TOUCH, down);
    buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
    cuttlefish::WriteAll(input_sockets_.GetTouchClientByLabel(display_label),
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
  }

  void OnMultiTouchEvent(const std::string &display_label, Json::Value id,
                         Json::Value slot, Json::Value x, Json::Value y,
                         bool down, int size) {
    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }

    for (int i = 0; i < size; i++) {
      auto this_slot = slot[i].asInt();
      auto this_id = id[i].asInt();
      auto this_x = x[i].asInt();
      auto this_y = y[i].asInt();

      if (confui_input_.IsConfUiActive()) {
        if (down) {
          confui_input_.TouchEvent(this_x, this_y, down);
        }
        continue;
      }

      buffer->AddEvent(EV_ABS, ABS_MT_SLOT, this_slot);
      if (down) {
        bool is_new = active_touch_slots_.insert(this_slot).second;
        if (is_new) {
          buffer->AddEvent(EV_ABS, ABS_MT_TRACKING_ID, this_id);
          if (active_touch_slots_.size() == 1) {
            buffer->AddEvent(EV_KEY, BTN_TOUCH, 1);
          }
        }
        buffer->AddEvent(EV_ABS, ABS_MT_POSITION_X, this_x);
        buffer->AddEvent(EV_ABS, ABS_MT_POSITION_Y, this_y);
        // send ABS_X and ABS_Y for single-touch compatibility
        buffer->AddEvent(EV_ABS, ABS_X, this_x);
        buffer->AddEvent(EV_ABS, ABS_Y, this_y);
      } else {
        // released touch
        buffer->AddEvent(EV_ABS, ABS_MT_TRACKING_ID, this_id);
        active_touch_slots_.erase(this_slot);
        if (active_touch_slots_.empty()) {
          buffer->AddEvent(EV_KEY, BTN_TOUCH, 0);
        }
      }
    }

    buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
    cuttlefish::WriteAll(input_sockets_.GetTouchClientByLabel(display_label),
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
  }

  void OnKeyboardEvent(uint16_t code, bool down) override {
    if (confui_input_.IsConfUiActive()) {
      ConfUiLog(VERBOSE) << "keyboard event ignored in confirmation UI mode";
      return;
    }

    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }
    buffer->AddEvent(EV_KEY, code, down);
    buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
    cuttlefish::WriteAll(input_sockets_.keyboard_client,
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
  }

  void OnSwitchEvent(uint16_t code, bool state) {
    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }
    buffer->AddEvent(EV_SW, code, state);
    buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
    cuttlefish::WriteAll(input_sockets_.switches_client,
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
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
  void OnControlChannelOpen(std::function<bool(const Json::Value)>
                            control_message_sender) override {
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
      cuttlefish::WriteAll(commands_to_custom_action_servers_[command],
                           action_server_message.c_str(), 128);
    } else {
      LOG(WARNING) << "Unsupported control command: " << command << " ("
                   << button_state << ")";
    }
  }

  void OnBluetoothChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                  bluetooth_message_sender) override {
    LOG(VERBOSE) << "Bluetooth channel open";
    auto config = cuttlefish::CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    bluetooth_handler_.reset(new cuttlefish::webrtc_streaming::BluetoothHandler(
        config->rootcanal_test_port(), bluetooth_message_sender));
  }

  void OnBluetoothMessage(const uint8_t *msg, size_t size) override {
    bluetooth_handler_->handleMessage(msg, size);
  }
  void OnLocationChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                 location_message_sender) override {
    LOG(VERBOSE) << "Location channel open";
    auto config = cuttlefish::CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    location_handler_.reset(new cuttlefish::webrtc_streaming::LocationHandler(
        location_message_sender));
  }
  void OnLocationMessage(const uint8_t *msg, size_t size) override {
    std::string msgstr(msg, msg + size);

    std::vector<std::string> inputs = android::base::Split(msgstr, ",");

    if(inputs.size() != 3){
      LOG(WARNING) << "Invalid location length , length = " << inputs.size();
      return;
    }

    float longitude = std::stod(inputs.at(0));
    float latitude  = std::stod(inputs.at(1));
    float elevation = std::stod(inputs.at(2));
    location_handler_->HandleMessage(longitude, latitude, elevation);
  }

  void OnKmlLocationsChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                     kml_locations_message_sender) override {
    LOG(VERBOSE) << "Kml Locations channel open";
    auto config = cuttlefish::CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    kml_locations_handler_.reset(
        new cuttlefish::webrtc_streaming::KmlLocationsHandler(
            kml_locations_message_sender));
  }
  void OnKmlLocationsMessage(const uint8_t *msg, size_t size) override {
    kml_locations_handler_->HandleMessage(msg, size);
  }

  void OnGpxLocationsChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                     gpx_locations_message_sender) override {
    LOG(VERBOSE) << "Gpx Locations channel open";
    auto config = cuttlefish::CuttlefishConfig::Get();
    CHECK(config) << "Failed to get config";
    gpx_locations_handler_.reset(
        new cuttlefish::webrtc_streaming::GpxLocationsHandler(
            gpx_locations_message_sender));
  }
  void OnGpxLocationsMessage(const uint8_t *msg, size_t size) override {
    gpx_locations_handler_->HandleMessage(msg, size);
  }

  void OnCameraControlMsg(const Json::Value& msg) override {
    if (camera_controller_) {
      camera_controller_->HandleMessage(msg);
    } else {
      LOG(VERBOSE) << "Camera control message received but no camera "
                      "controller is available";
    }
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
  cuttlefish::InputSockets& input_sockets_;
  cuttlefish::KernelLogEventsHandler* kernel_log_events_handler_;
  int kernel_log_subscription_id_ = -1;
  std::shared_ptr<cuttlefish::webrtc_streaming::AdbHandler> adb_handler_;
  std::shared_ptr<cuttlefish::webrtc_streaming::BluetoothHandler>
      bluetooth_handler_;
  std::shared_ptr<cuttlefish::webrtc_streaming::LocationHandler>
      location_handler_;
  std::shared_ptr<cuttlefish::webrtc_streaming::KmlLocationsHandler>
      kml_locations_handler_;
  std::shared_ptr<cuttlefish::webrtc_streaming::GpxLocationsHandler>
      gpx_locations_handler_;
  std::map<std::string, cuttlefish::SharedFD> commands_to_custom_action_servers_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
  std::set<int32_t> active_touch_slots_;
  cuttlefish::CameraController *camera_controller_;
  cuttlefish::confui::HostVirtualInput &confui_input_;
};

CfConnectionObserverFactory::CfConnectionObserverFactory(
    cuttlefish::InputSockets &input_sockets,
    cuttlefish::KernelLogEventsHandler* kernel_log_events_handler,
    cuttlefish::confui::HostVirtualInput &confui_input)
    : input_sockets_(input_sockets),
      kernel_log_events_handler_(kernel_log_events_handler),
      confui_input_{confui_input} {}

std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>
CfConnectionObserverFactory::CreateObserver() {
  return std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>(
      new ConnectionObserverImpl(input_sockets_, kernel_log_events_handler_,
                                 commands_to_custom_action_servers_,
                                 weak_display_handler_, camera_controller_,
                                 confui_input_));
}

void CfConnectionObserverFactory::AddCustomActionServer(
    cuttlefish::SharedFD custom_action_server_fd,
    const std::vector<std::string>& commands) {
  for (const std::string& command : commands) {
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

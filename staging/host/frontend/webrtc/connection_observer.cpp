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
#include "host/frontend/webrtc/lib/utils.h"
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
class ConnectionObserverForAndroid
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  ConnectionObserverForAndroid(
      cuttlefish::InputSockets &input_sockets,
      cuttlefish::KernelLogEventsHandler *kernel_log_events_handler,
      std::map<std::string, cuttlefish::SharedFD>
          commands_to_custom_action_servers,
      std::weak_ptr<DisplayHandler> display_handler)
      : input_sockets_(input_sockets),
        kernel_log_events_handler_(kernel_log_events_handler),
        commands_to_custom_action_servers_(commands_to_custom_action_servers),
        weak_display_handler_(display_handler) {}
  virtual ~ConnectionObserverForAndroid() {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      display_handler->DecClientCount();
    }
    if (kernel_log_subscription_id_ != -1) {
      kernel_log_events_handler_->Unsubscribe(kernel_log_subscription_id_);
    }
  }

  void OnConnected(std::function<void(const uint8_t *, size_t, bool)>
                   /*ctrl_msg_sender*/) override {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      display_handler->IncClientCount();
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

  void OnTouchEvent(const std::string & /*display_label*/, int x, int y,
                    bool down) override {

    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }
    buffer->AddEvent(EV_ABS, ABS_X, x);
    buffer->AddEvent(EV_ABS, ABS_Y, y);
    buffer->AddEvent(EV_KEY, BTN_TOUCH, down);
    buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
    cuttlefish::WriteAll(input_sockets_.touch_client,
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
  }

  void OnMultiTouchEvent(const std::string & /*display_label*/, Json::Value id,
                         Json::Value slot, Json::Value x, Json::Value y,
                         bool down, int size) override {

    auto buffer = GetEventBuffer();
    if (!buffer) {
      LOG(ERROR) << "Failed to allocate event buffer";
      return;
    }

    for (int i=0; i<size; i++) {
      auto this_slot = slot[i].asInt();
      auto this_id = id[i].asInt();
      auto this_x = x[i].asInt();
      auto this_y = y[i].asInt();
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
    cuttlefish::WriteAll(input_sockets_.touch_client,
                         reinterpret_cast<const char *>(buffer->data()),
                         buffer->size());
  }

  void OnKeyboardEvent(uint16_t code, bool down) override {
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

  void OnSwitchEvent(uint16_t code, bool state) override {
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
    kernel_log_subscription_id_ =
        kernel_log_events_handler_->AddSubscriber(control_message_sender);
  }
  void OnControlMessage(const uint8_t* msg, size_t size) override {
    Json::Value evt;
    const char* msg_str = reinterpret_cast<const char*>(msg);
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
    std::string errorMessage;
    if (!json_reader->parse(msg_str, msg_str + size, &evt, &errorMessage)) {
      LOG(ERROR) << "Received invalid JSON object over control channel: " << errorMessage;
      return;
    }

    auto result = webrtc_streaming::ValidationResult::ValidateJsonObject(
        evt, "command",
        /*required_fields=*/{{"command", Json::ValueType::stringValue}},
        /*optional_fields=*/
        {
            {"button_state", Json::ValueType::stringValue},
            {"lid_switch_open", Json::ValueType::booleanValue},
            {"hinge_angle_value", Json::ValueType::intValue},
        });
    if (!result.ok()) {
      LOG(ERROR) << result.error();
      return;
    }
    auto command = evt["command"].asString();

    if (command == "device_state") {
      if (evt.isMember("lid_switch_open")) {
        // InputManagerService treats a value of 0 as open and 1 as closed, so
        // invert the lid_switch_open value that is sent to the input device.
        OnSwitchEvent(SW_LID, !evt["lid_switch_open"].asBool());
      }
      if (evt.isMember("hinge_angle_value")) {
        // TODO(b/181157794) Propagate hinge angle sensor data using a custom
        // Sensor HAL.
      }
      return;
    }

    auto button_state = evt["button_state"].asString();
    LOG(VERBOSE) << "Control command: " << command << " (" << button_state
                 << ")";
    if (command == "power") {
      OnKeyboardEvent(KEY_POWER, button_state == "down");
    } else if (command == "home") {
      OnKeyboardEvent(KEY_HOMEPAGE, button_state == "down");
    } else if (command == "menu") {
      OnKeyboardEvent(KEY_MENU, button_state == "down");
    } else if (command == "volumemute") {
      OnKeyboardEvent(KEY_MUTE, button_state == "down");
    } else if (command == "volumedown") {
      OnKeyboardEvent(KEY_VOLUMEDOWN, button_state == "down");
    } else if (command == "volumeup") {
      OnKeyboardEvent(KEY_VOLUMEUP, button_state == "down");
    } else if (commands_to_custom_action_servers_.find(command) !=
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
    bluetooth_handler_.reset(new cuttlefish::webrtc_streaming::BluetoothHandler(
        cuttlefish::CuttlefishConfig::Get()
            ->ForDefaultInstance()
            .rootcanal_test_port(),
        bluetooth_message_sender));
  }

  void OnBluetoothMessage(const uint8_t *msg, size_t size) override {
    bluetooth_handler_->handleMessage(msg, size);
  }

 private:
  cuttlefish::InputSockets& input_sockets_;
  cuttlefish::KernelLogEventsHandler* kernel_log_events_handler_;
  int kernel_log_subscription_id_ = -1;
  std::shared_ptr<cuttlefish::webrtc_streaming::AdbHandler> adb_handler_;
  std::shared_ptr<cuttlefish::webrtc_streaming::BluetoothHandler>
      bluetooth_handler_;
  std::map<std::string, cuttlefish::SharedFD> commands_to_custom_action_servers_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
  std::set<int32_t> active_touch_slots_;
};

class ConnectionObserverDemuxer
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  ConnectionObserverDemuxer(
      /* params for the base class */
      cuttlefish::InputSockets &input_sockets,
      cuttlefish::KernelLogEventsHandler *kernel_log_events_handler,
      std::map<std::string, cuttlefish::SharedFD>
          commands_to_custom_action_servers,
      std::weak_ptr<DisplayHandler> display_handler,
      /* params for this class */
      cuttlefish::confui::HostVirtualInput &confui_input)
      : android_input_(input_sockets, kernel_log_events_handler,
                       commands_to_custom_action_servers, display_handler),
        confui_input_{confui_input} {}
  virtual ~ConnectionObserverDemuxer() = default;

  void OnConnected(std::function<void(const uint8_t *, size_t, bool)>
                       ctrl_msg_sender) override {
    android_input_.OnConnected(ctrl_msg_sender);
  }

  void OnTouchEvent(const std::string &label, int x, int y,
                    bool down) override {
    if (confui_input_.IsConfUiActive()) {
      ConfUiLog(DEBUG) << "touch event ignored in confirmation UI mode";
      return;
    }
    android_input_.OnTouchEvent(label, x, y, down);
  }

  void OnMultiTouchEvent(const std::string &label, Json::Value id,
                         Json::Value slot, Json::Value x, Json::Value y,
                         bool down, int size) override {
    if (confui_input_.IsConfUiActive()) {
      ConfUiLog(DEBUG) << "multi-touch event ignored in confirmation UI mode";
      return;
    }
    android_input_.OnMultiTouchEvent(label, id, slot, x, y, down, size);
  }

  void OnKeyboardEvent(uint16_t code, bool down) override {
    if (confui_input_.IsConfUiActive()) {
      switch (code) {
        case KEY_POWER:
          confui_input_.PressConfirmButton(down);
          break;
        case KEY_MENU:
          confui_input_.PressCancelButton(down);
          break;
        default:
          ConfUiLog(DEBUG) << "key" << code
                           << "is ignored in confirmation UI mode";
          break;
      }
      return;
    }
    android_input_.OnKeyboardEvent(code, down);
  }

  void OnSwitchEvent(uint16_t code, bool state) override {
    android_input_.OnSwitchEvent(code, state);
  }

  void OnAdbChannelOpen(std::function<bool(const uint8_t *, size_t)>
                            adb_message_sender) override {
    android_input_.OnAdbChannelOpen(adb_message_sender);
  }

  void OnAdbMessage(const uint8_t *msg, size_t size) override {
    android_input_.OnAdbMessage(msg, size);
  }

  void OnControlChannelOpen(
      std::function<bool(const Json::Value)> control_message_sender) override {
    android_input_.OnControlChannelOpen(control_message_sender);
  }

  void OnControlMessage(const uint8_t *msg, size_t size) override {
    android_input_.OnControlMessage(msg, size);
  }

  void OnBluetoothChannelOpen(std::function<bool(const uint8_t *, size_t)>
                                  bluetooth_message_sender) override {
    android_input_.OnBluetoothChannelOpen(bluetooth_message_sender);
  }

  void OnBluetoothMessage(const uint8_t *msg, size_t size) override {
    android_input_.OnBluetoothMessage(msg, size);
  }

 private:
  ConnectionObserverForAndroid android_input_;
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
      new ConnectionObserverDemuxer(input_sockets_, kernel_log_events_handler_,
                                    commands_to_custom_action_servers_,
                                    weak_display_handler_, confui_input_));
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
}  // namespace cuttlefish

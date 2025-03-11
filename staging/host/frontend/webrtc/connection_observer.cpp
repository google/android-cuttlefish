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

#include <map>
#include <set>
#include <thread>
#include <vector>

#include <json/json.h>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "host/frontend/webrtc/adb_handler.h"
#include "host/frontend/webrtc/kernel_log_events_handler.h"
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

class ConnectionObserverImpl
    : public cuttlefish::webrtc_streaming::ConnectionObserver {
 public:
  ConnectionObserverImpl(cuttlefish::InputSockets& input_sockets,
                         cuttlefish::SharedFD kernel_log_events_fd,
                         std::map<std::string, cuttlefish::SharedFD>
                             commands_to_custom_action_servers,
                         std::weak_ptr<DisplayHandler> display_handler)
      : input_sockets_(input_sockets),
        kernel_log_events_client_(kernel_log_events_fd),
        commands_to_custom_action_servers_(commands_to_custom_action_servers),
        weak_display_handler_(display_handler) {}
  virtual ~ConnectionObserverImpl() {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      display_handler->DecClientCount();
    }
  }

  void OnConnected(std::function<void(const uint8_t *, size_t, bool)>
                   /*ctrl_msg_sender*/) override {
    auto display_handler = weak_display_handler_.lock();
    if (display_handler) {
      display_handler->IncClientCount();
      // A long time may pass before the next frame comes up from the guest.
      // Send the last one to avoid showing a black screen to the user during
      // that time.
      display_handler->SendLastFrame();
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
    kernel_log_events_handler_.reset(new cuttlefish::webrtc_streaming::KernelLogEventsHandler(
        kernel_log_events_client_,
        control_message_sender));
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
    } else if (command == "home") {
      OnKeyboardEvent(KEY_HOMEPAGE, state == "down");
    } else if (command == "menu") {
      OnKeyboardEvent(KEY_MENU, state == "down");
    } else if (command == "volumemute") {
      OnKeyboardEvent(KEY_MUTE, state == "down");
    } else if (command == "volumedown") {
      OnKeyboardEvent(KEY_VOLUMEDOWN, state == "down");
    } else if (command == "volumeup") {
      OnKeyboardEvent(KEY_VOLUMEUP, state == "down");
    } else if (commands_to_custom_action_servers_.find(command) !=
               commands_to_custom_action_servers_.end()) {
      // Simple protocol for commands forwarded to action servers:
      //   - Always 128 bytes
      //   - Format:   command:state
      //   - Example:  my_button:down
      std::string action_server_message = command + ":" + state;
      cuttlefish::WriteAll(commands_to_custom_action_servers_[command],
                           action_server_message.c_str(), 128);
    } else {
      LOG(WARNING) << "Unsupported control command: " << command << " (" << state << ")";
      // TODO(b/163081337): Handle custom commands.
    }
  }

 private:
  cuttlefish::InputSockets& input_sockets_;
  cuttlefish::SharedFD kernel_log_events_client_;
  std::shared_ptr<cuttlefish::webrtc_streaming::AdbHandler> adb_handler_;
  std::shared_ptr<cuttlefish::webrtc_streaming::KernelLogEventsHandler> kernel_log_events_handler_;
  std::map<std::string, cuttlefish::SharedFD> commands_to_custom_action_servers_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
  std::set<int32_t> active_touch_slots_;
};

CfConnectionObserverFactory::CfConnectionObserverFactory(
    cuttlefish::InputSockets& input_sockets,
    cuttlefish::SharedFD kernel_log_events_fd)
    : input_sockets_(input_sockets),
      kernel_log_events_fd_(kernel_log_events_fd) {}

std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>
CfConnectionObserverFactory::CreateObserver() {
  return std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver>(
      new ConnectionObserverImpl(input_sockets_,
                                 kernel_log_events_fd_,
                                 commands_to_custom_action_servers_,
                                 weak_display_handler_));
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

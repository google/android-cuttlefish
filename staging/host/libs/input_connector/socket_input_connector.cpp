/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/input_connector/socket_input_connector.h"

#include <linux/input.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

namespace {

struct virtio_input_event {
  uint16_t type;
  uint16_t code;
  int32_t value;
};

struct InputEventsBuffer {
  virtual ~InputEventsBuffer() = default;
  virtual void AddEvent(uint16_t type, uint16_t code, int32_t value) = 0;
  virtual size_t size() const = 0;
  virtual const void* data() const = 0;
};

template <typename T>
struct InputEventsBufferImpl : public InputEventsBuffer {
  InputEventsBufferImpl(size_t num_events) { buffer_.reserve(num_events); }
  void AddEvent(uint16_t type, uint16_t code, int32_t value) override {
    buffer_.push_back({.type = type, .code = code, .value = value});
  }
  T* data() { return buffer_.data(); }
  const void* data() const override { return buffer_.data(); }
  std::size_t size() const override { return buffer_.size() * sizeof(T); }

 private:
  std::vector<T> buffer_;
};

std::unique_ptr<InputEventsBuffer> CreateBuffer(InputEventType event_type,
                                                size_t num_events) {
  switch (event_type) {
    case InputEventType::Virtio:
      return std::unique_ptr<InputEventsBuffer>(
          new InputEventsBufferImpl<virtio_input_event>(num_events));
    case InputEventType::Evdev:
      return std::unique_ptr<InputEventsBuffer>(
          new InputEventsBufferImpl<input_event>(num_events));
  }
}

}  // namespace

class InputSocket {
 public:
  InputSocket(SharedFD server)
      : server_(server), monitor_(std::thread([this]() { MonitorLoop(); })) {}

  Result<void> WriteEvents(std::unique_ptr<InputEventsBuffer> buffer);

 private:
  SharedFD server_;
  SharedFD client_;
  std::mutex client_mtx_;
  std::thread monitor_;

  void MonitorLoop();
};

void InputSocket::MonitorLoop() {
  for (;;) {
    client_ = SharedFD::Accept(*server_);
    if (!client_->IsOpen()) {
      LOG(ERROR) << "Failed to accept on input socket: " << client_->StrError();
      continue;
    }
    do {
      // Keep reading from the fd to detect when it closes.
      char buf[128];
      auto res = client_->Read(buf, sizeof(buf));
      if (res < 0) {
        LOG(ERROR) << "Failed to read from input client: "
                   << client_->StrError();
      } else if (res > 0) {
        LOG(VERBOSE) << "Received " << res << " bytes on input socket";
      } else {
        std::lock_guard<std::mutex> lock(client_mtx_);
        client_->Close();
      }
    } while (client_->IsOpen());
  }
}

Result<void> InputSocket::WriteEvents(
    std::unique_ptr<InputEventsBuffer> buffer) {
  std::lock_guard<std::mutex> lock(client_mtx_);
  CF_EXPECT(client_->IsOpen(), "No input client connected");
  auto res = WriteAll(client_, reinterpret_cast<const char*>(buffer->data()),
                      buffer->size());
  CF_EXPECT(res == buffer->size(), "Failed to write entire event buffer: wrote "
                                       << res << " of " << buffer->size()
                                       << "bytes");
  return {};
}

struct Touchscreen {
  std::unique_ptr<InputSocket> socket;
  std::set<int32_t> active_slots;
};

// Implements the InputConnector interface using unix socket based virtual input
// devices.
class InputSocketsConnector : public InputConnector {
 public:
  Result<void> SendTouchEvent(const std::string& display, int x, int y,
                              bool down) override;
  Result<void> SendMultiTouchEvent(const std::string& display_label,
                                   const std::vector<MultitouchSlot>& slots,
                                   bool down) override;
  Result<void> SendKeyboardEvent(uint16_t code, bool down) override;
  Result<void> SendRotaryEvent(int pixels) override;
  Result<void> SendSwitchesEvent(uint16_t code, bool state) override;

 private:
  InputEventType event_type_;
  // TODO (b/186773052): Finding strings in a map for every input event may
  // introduce unwanted latency.
  std::map<std::string, Touchscreen> touchscreens_;

  std::unique_ptr<InputSocket> keyboard_;
  std::unique_ptr<InputSocket> switches_;
  std::unique_ptr<InputSocket> rotary_;

  InputSocketsConnector(InputEventType event_type);

  friend class InputSocketsConnectorBuilder;
};

InputSocketsConnector::InputSocketsConnector(InputEventType event_type)
    : event_type_(event_type) {}

Result<void> InputSocketsConnector::SendTouchEvent(const std::string& display,
                                                   int x, int y, bool down) {
  auto buffer = CreateBuffer(event_type_, 4);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_ABS, ABS_X, x);
  buffer->AddEvent(EV_ABS, ABS_Y, y);
  buffer->AddEvent(EV_KEY, BTN_TOUCH, down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  auto ts_it = touchscreens_.find(display);
  CF_EXPECT(ts_it != touchscreens_.end(), "Unknown display: " << display);
  auto& ts = ts_it->second;
  ts.socket->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsConnector::SendMultiTouchEvent(
    const std::string& display, const std::vector<MultitouchSlot>& slots,
    bool down) {
  auto buffer = CreateBuffer(event_type_, 1 + 7 * slots.size());
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");

  auto ts_it = touchscreens_.find(display);
  CF_EXPECT(ts_it != touchscreens_.end(), "Unknown display: " << display);
  auto& ts = ts_it->second;

  for (auto& f : slots) {
    auto this_slot = f.slot;
    auto this_id = f.id;
    auto this_x = f.x;
    auto this_y = f.y;

    buffer->AddEvent(EV_ABS, ABS_MT_SLOT, this_slot);
    if (down) {
      bool is_new = ts.active_slots.insert(this_slot).second;
      if (is_new) {
        buffer->AddEvent(EV_ABS, ABS_MT_TRACKING_ID, this_id);
        if (ts.active_slots.size() == 1) {
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
      ts.active_slots.erase(this_slot);
      if (ts.active_slots.empty()) {
        buffer->AddEvent(EV_KEY, BTN_TOUCH, 0);
      }
    }
  }

  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  ts.socket->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsConnector::SendKeyboardEvent(uint16_t code,
                                                      bool down) {
  CF_EXPECT(keyboard_ != nullptr, "No keyboard device setup");
  auto buffer = CreateBuffer(event_type_, 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_KEY, code, down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  keyboard_->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsConnector::SendRotaryEvent(int pixels) {
  CF_EXPECT(rotary_ != nullptr, "No rotary device setup");
  auto buffer = CreateBuffer(event_type_, 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_REL, REL_WHEEL, pixels);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  rotary_->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsConnector::SendSwitchesEvent(uint16_t code,
                                                      bool state) {
  CF_EXPECT(switches_ != nullptr, "No switches device setup");
  auto buffer = CreateBuffer(event_type_, 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_SW, code, state);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  switches_->WriteEvents(std::move(buffer));
  return {};
}

InputSocketsConnectorBuilder::InputSocketsConnectorBuilder(InputEventType type)
    : connector_(new InputSocketsConnector(type)) {}

InputSocketsConnectorBuilder::~InputSocketsConnectorBuilder() = default;

void InputSocketsConnectorBuilder::WithTouchscreen(const std::string& display,
                                                   SharedFD server) {
  CHECK(connector_->touchscreens_.find(display) ==
        connector_->touchscreens_.end())
      << "Multiple displays with same label: " << display;
  connector_->touchscreens_.emplace(
      display, Touchscreen{.socket = std::make_unique<InputSocket>(server)});
}

void InputSocketsConnectorBuilder::WithKeyboard(SharedFD server) {
  CHECK(!connector_->keyboard_) << "Keyboard already specified";
  connector_->keyboard_.reset(new InputSocket(server));
}

void InputSocketsConnectorBuilder::WithSwitches(SharedFD server) {
  CHECK(!connector_->switches_) << "Switches already specified";
  connector_->switches_.reset(new InputSocket(server));
}

void InputSocketsConnectorBuilder::WithRotary(SharedFD server) {
  CHECK(!connector_->rotary_) << "Rotary already specified";
  connector_->rotary_.reset(new InputSocket(server));
}

std::unique_ptr<InputConnector> InputSocketsConnectorBuilder::Build() && {
  return std::move(connector_);
}

}  // namespace cuttlefish

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
#include "host/libs/config/cuttlefish_config.h"

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

class TouchDevice {
 public:
  TouchDevice(std::unique_ptr<InputSocket> s) : socket_(std::move(s)) {}

  Result<void> WriteEvents(std::unique_ptr<InputEventsBuffer> buffer) {
    return socket_->WriteEvents(std::move(buffer));
  }

  bool HasSlot(void* source, int32_t id) {
    std::lock_guard<std::mutex> lock(slots_mtx_);
    return slots_by_source_and_id_.find({source, id}) !=
           slots_by_source_and_id_.end();
  }

  int32_t GetOrAcquireSlot(void* source, int32_t id) {
    std::lock_guard<std::mutex> lock(slots_mtx_);
    auto slot_it = slots_by_source_and_id_.find({source, id});
    if (slot_it != slots_by_source_and_id_.end()) {
      return slot_it->second;
    }
    return slots_by_source_and_id_[std::make_pair(source, id)] = UseNewSlot();
  }

  void ReleaseSlot(void* source, int32_t id) {
    std::lock_guard<std::mutex> lock(slots_mtx_);
    auto slot_it = slots_by_source_and_id_.find({source, id});
    if (slot_it == slots_by_source_and_id_.end()) {
      return;
    }
    slots_by_source_and_id_.erase(slot_it);
    active_slots_[slot_it->second] = false;
  }

  size_t NumActiveSlots() {
    std::lock_guard<std::mutex> lock(slots_mtx_);
    return slots_by_source_and_id_.size();
  }

  // The InputConnector holds state of on-going touch contacts. Event sources
  // that can produce multi touch events should call this function when it's
  // known they won't produce any more events (because, for example, the
  // streaming client disconnected) to make sure no stale touch contacts
  // remain. This addresses issues arising from clients disconnecting in the
  // middle of a touch action.
  void OnDisconnectedSource(void* source) {
    std::lock_guard<std::mutex> lock(slots_mtx_);
    auto it = slots_by_source_and_id_.begin();
    while (it != slots_by_source_and_id_.end()) {
      if (it->first.first == source) {
        active_slots_[it->second] = false;
        it = slots_by_source_and_id_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  int32_t UseNewSlot() {
    // This is not the most efficient implementation for a large number of
    // slots, but that case should be extremely rare. For the typical number of
    // slots iterating over a vector is likely faster than using other data
    // structures.
    for (auto slot = 0; slot < active_slots_.size(); ++slot) {
      if (!active_slots_[slot]) {
        active_slots_[slot] = true;
        return slot;
      }
    }
    active_slots_.push_back(true);
    return active_slots_.size() - 1;
  }

  std::unique_ptr<InputSocket> socket_;

  std::mutex slots_mtx_;
  std::map<std::pair<void*, int32_t>, int32_t> slots_by_source_and_id_;
  std::vector<bool> active_slots_;
};

struct InputDevices {
  InputEventType event_type;
  // TODO (b/186773052): Finding strings in a map for every input event may
  // introduce unwanted latency.
  std::map<std::string, TouchDevice> multitouch_devices;
  std::map<std::string, TouchDevice> touch_devices;

  std::unique_ptr<InputSocket> keyboard;
  std::unique_ptr<InputSocket> switches;
  std::unique_ptr<InputSocket> rotary;
  std::unique_ptr<InputSocket> mouse;
};

// Implements the InputConnector::EventSink interface using unix socket based
// virtual input devices.
class InputSocketsEventSink : public InputConnector::EventSink {
 public:
  InputSocketsEventSink(InputDevices&, std::atomic<int>&);
  ~InputSocketsEventSink() override;

  Result<void> SendMouseMoveEvent(int x, int y) override;
  Result<void> SendMouseButtonEvent(int button, bool down) override;
  Result<void> SendTouchEvent(const std::string& device_label, int x, int y,
                              bool down) override;
  Result<void> SendMultiTouchEvent(const std::string& device_label,
                                   const std::vector<MultitouchSlot>& slots,
                                   bool down) override;
  Result<void> SendKeyboardEvent(uint16_t code, bool down) override;
  Result<void> SendRotaryEvent(int pixels) override;
  Result<void> SendSwitchesEvent(uint16_t code, bool state) override;

 private:
  InputDevices& input_devices_;
  std::atomic<int>& sinks_count_;
};

InputSocketsEventSink::InputSocketsEventSink(InputDevices& devices,
                                             std::atomic<int>& count)
    : input_devices_(devices), sinks_count_(count) {
  ++sinks_count_;
}

InputSocketsEventSink::~InputSocketsEventSink() {
  for (auto& it : input_devices_.multitouch_devices) {
    it.second.OnDisconnectedSource(this);
  }
  for (auto& it : input_devices_.touch_devices) {
    it.second.OnDisconnectedSource(this);
  }
  --sinks_count_;
}

Result<void> InputSocketsEventSink::SendMouseMoveEvent(int x, int y) {
  CF_EXPECT(input_devices_.mouse != nullptr, "No mouse device setup");
  auto buffer = CreateBuffer(input_devices_.event_type, 2);
  CF_EXPECT(buffer != nullptr,
            "Failed to allocate input events buffer for mouse move event !");
  buffer->AddEvent(EV_REL, REL_X, x);
  buffer->AddEvent(EV_REL, REL_Y, y);
  input_devices_.mouse->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsEventSink::SendMouseButtonEvent(int button,
                                                         bool down) {
  CF_EXPECT(input_devices_.mouse != nullptr, "No mouse device setup");
  auto buffer = CreateBuffer(input_devices_.event_type, 2);
  CF_EXPECT(buffer != nullptr,
            "Failed to allocate input events buffer for mouse button event !");
  std::vector<int> buttons = {BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, BTN_BACK,
                              BTN_FORWARD};
  CF_EXPECT(button < (int)buttons.size(),
            "Unknown mouse event button: " << button);
  buffer->AddEvent(EV_KEY, buttons[button], down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  input_devices_.mouse->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsEventSink::SendTouchEvent(
    const std::string& device_label, int x, int y, bool down) {
  auto buffer = CreateBuffer(input_devices_.event_type, 4);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_ABS, ABS_X, x);
  buffer->AddEvent(EV_ABS, ABS_Y, y);
  buffer->AddEvent(EV_KEY, BTN_TOUCH, down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  auto ts_it = input_devices_.touch_devices.find(device_label);
  CF_EXPECT(ts_it != input_devices_.touch_devices.end(),
            "Unknown touch device: " << device_label);
  auto& ts = ts_it->second;
  ts.WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsEventSink::SendMultiTouchEvent(
    const std::string& device_label, const std::vector<MultitouchSlot>& slots,
    bool down) {
  auto buffer = CreateBuffer(input_devices_.event_type, 1 + 7 * slots.size());
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");

  auto ts_it = input_devices_.multitouch_devices.find(device_label);
  if (ts_it == input_devices_.multitouch_devices.end()) {
    for (const auto& slot : slots) {
      CF_EXPECT(SendTouchEvent(device_label, slot.x, slot.y, down));
    }
    return {};
  }
  auto& ts = ts_it->second;

  for (auto& f : slots) {
    auto this_id = f.id;
    auto this_x = f.x;
    auto this_y = f.y;

    auto is_new_contact = !ts.HasSlot(this, this_id);
    auto was_down = ts.NumActiveSlots() > 0;

    // Make sure to call HasSlot before this line or it will always return true
    auto this_slot = ts.GetOrAcquireSlot(this, this_id);

    // BTN_TOUCH DOWN must be the first event in a series
    if (down && !was_down) {
      buffer->AddEvent(EV_KEY, BTN_TOUCH, 1);
    }

    buffer->AddEvent(EV_ABS, ABS_MT_SLOT, this_slot);
    if (down) {
      if (is_new_contact) {
        // We already assigned this slot to this source and id combination, we
        // could use any tracking id for the slot as long as it's greater than 0
        buffer->AddEvent(EV_ABS, ABS_MT_TRACKING_ID, this_id);
      }
      buffer->AddEvent(EV_ABS, ABS_MT_POSITION_X, this_x);
      buffer->AddEvent(EV_ABS, ABS_MT_POSITION_Y, this_y);
    } else {
      // released touch
      buffer->AddEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
      ts.ReleaseSlot(this, this_id);
    }
    // Send BTN_TOUCH UP when no more contacts are detected
    if (was_down && ts.NumActiveSlots() == 0) {
      buffer->AddEvent(EV_KEY, BTN_TOUCH, 0);
    }
  }

  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  ts.WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsEventSink::SendKeyboardEvent(uint16_t code,
                                                      bool down) {
  CF_EXPECT(input_devices_.keyboard != nullptr, "No keyboard device setup");
  auto buffer = CreateBuffer(input_devices_.event_type, 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_KEY, code, down);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  input_devices_.keyboard->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsEventSink::SendRotaryEvent(int pixels) {
  CF_EXPECT(input_devices_.rotary != nullptr, "No rotary device setup");
  auto buffer = CreateBuffer(input_devices_.event_type, 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_REL, REL_WHEEL, pixels);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  input_devices_.rotary->WriteEvents(std::move(buffer));
  return {};
}

Result<void> InputSocketsEventSink::SendSwitchesEvent(uint16_t code,
                                                      bool state) {
  CF_EXPECT(input_devices_.switches != nullptr, "No switches device setup");
  auto buffer = CreateBuffer(input_devices_.event_type, 2);
  CF_EXPECT(buffer != nullptr, "Failed to allocate input events buffer");
  buffer->AddEvent(EV_SW, code, state);
  buffer->AddEvent(EV_SYN, SYN_REPORT, 0);
  input_devices_.switches->WriteEvents(std::move(buffer));
  return {};
}

class InputSocketsConnector : public InputConnector {
 public:
  InputSocketsConnector(InputEventType type) : devices_{.event_type = type} {}
  ~InputSocketsConnector();

  std::unique_ptr<EventSink> CreateSink() override;

 private:
  InputDevices devices_;
  // Counts the number of events sinks to make sure the class is not destroyed
  // while any of its sinks still exists.
  std::atomic<int> sinks_count_ = 0;
  friend class InputSocketsConnectorBuilder;
};

InputSocketsConnector::~InputSocketsConnector() {
  CHECK(sinks_count_ == 0) << "Input connector destroyed with " << sinks_count_
                           << " event sinks left";
}

std::unique_ptr<InputConnector::EventSink> InputSocketsConnector::CreateSink() {
  return std::unique_ptr<InputConnector::EventSink>(
      new InputSocketsEventSink(devices_, sinks_count_));
}

InputSocketsConnectorBuilder::InputSocketsConnectorBuilder(InputEventType type)
    : connector_(new InputSocketsConnector(type)) {}

InputSocketsConnectorBuilder::~InputSocketsConnectorBuilder() = default;

void InputSocketsConnectorBuilder::WithMultitouchDevice(
    const std::string& device_label, SharedFD server) {
  CHECK(connector_->devices_.multitouch_devices.find(device_label) ==
        connector_->devices_.multitouch_devices.end())
      << "Multiple touch devices with same label: " << device_label;
  connector_->devices_.multitouch_devices.emplace(
      device_label, std::make_unique<InputSocket>(server));
}

void InputSocketsConnectorBuilder::WithTouchDevice(
    const std::string& device_label, SharedFD server) {
  CHECK(connector_->devices_.touch_devices.find(device_label) ==
        connector_->devices_.touch_devices.end())
      << "Multiple touch devices with same label: " << device_label;
  connector_->devices_.touch_devices.emplace(device_label,
                                     std::make_unique<InputSocket>(server));
}

void InputSocketsConnectorBuilder::WithKeyboard(SharedFD server) {
  CHECK(!connector_->devices_.keyboard) << "Keyboard already specified";
  connector_->devices_.keyboard.reset(new InputSocket(server));
}

void InputSocketsConnectorBuilder::WithSwitches(SharedFD server) {
  CHECK(!connector_->devices_.switches) << "Switches already specified";
  connector_->devices_.switches.reset(new InputSocket(server));
}

void InputSocketsConnectorBuilder::WithRotary(SharedFD server) {
  CHECK(!connector_->devices_.rotary) << "Rotary already specified";
  connector_->devices_.rotary.reset(new InputSocket(server));
}

void InputSocketsConnectorBuilder::WithMouse(SharedFD server) {
  CHECK(!connector_->devices_.mouse) << "Mouse already specified";
  connector_->devices_.mouse.reset(new InputSocket(server));
}

std::unique_ptr<InputConnector> InputSocketsConnectorBuilder::Build() && {
  return std::move(connector_);
}

}  // namespace cuttlefish

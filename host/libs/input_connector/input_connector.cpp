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

#include "host/libs/input_connector/input_connector.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/input_connector/event_buffer.h"
#include "host/libs/input_connector/input_connection.h"
#include "host/libs/input_connector/input_devices.h"

namespace cuttlefish {

struct InputDevices {
  // TODO (b/186773052): Finding strings in a map for every input event may
  // introduce unwanted latency.
  std::map<std::string, TouchDevice> multitouch_devices;
  std::map<std::string, TouchDevice> touch_devices;

  std::optional<KeyboardDevice> keyboard;
  std::optional<SwitchesDevice> switches;
  std::optional<RotaryDevice> rotary;
  std::optional<MouseDevice> mouse;
};

class EventSinkImpl : public InputConnector::EventSink {
 public:
  EventSinkImpl(InputDevices&, std::atomic<int>&);
  ~EventSinkImpl() override;

  Result<void> SendMouseMoveEvent(int x, int y) override;
  Result<void> SendMouseButtonEvent(int button, bool down) override;
  Result<void> SendMouseWheelEvent(int pixels) override;
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

EventSinkImpl::EventSinkImpl(InputDevices& devices, std::atomic<int>& count)
    : input_devices_(devices), sinks_count_(count) {
  ++sinks_count_;
}

EventSinkImpl::~EventSinkImpl() {
  for (auto& it : input_devices_.multitouch_devices) {
    it.second.OnDisconnectedSource(this);
  }
  for (auto& it : input_devices_.touch_devices) {
    it.second.OnDisconnectedSource(this);
  }
  --sinks_count_;
}

Result<void> EventSinkImpl::SendMouseMoveEvent(int x, int y) {
  CF_EXPECT(input_devices_.mouse.has_value(), "No mouse device setup");
  CF_EXPECT(input_devices_.mouse->SendMoveEvent(x, y));
  return {};
}

Result<void> EventSinkImpl::SendMouseButtonEvent(int button, bool down) {
  CF_EXPECT(input_devices_.mouse.has_value(), "No mouse device setup");
  CF_EXPECT(input_devices_.mouse->SendButtonEvent(button, down));
  return {};
}

Result<void> EventSinkImpl::SendMouseWheelEvent(int pixels) {
  CF_EXPECT(input_devices_.mouse.has_value(), "No mouse device setup");
  CF_EXPECT(input_devices_.mouse->SendWheelEvent(pixels));
  return {};
}

Result<void> EventSinkImpl::SendTouchEvent(const std::string& device_label,
                                           int x, int y, bool down) {
  auto ts_it = input_devices_.touch_devices.find(device_label);
  CF_EXPECT(ts_it != input_devices_.touch_devices.end(),
            "Unknown touch device: " << device_label);
  auto& ts = ts_it->second;
  CF_EXPECT(ts.SendTouchEvent(x, y, down));
  return {};
}

Result<void> EventSinkImpl::SendMultiTouchEvent(
    const std::string& device_label, const std::vector<MultitouchSlot>& slots,
    bool down) {
  auto ts_it = input_devices_.multitouch_devices.find(device_label);
  if (ts_it == input_devices_.multitouch_devices.end()) {
    for (const auto& slot : slots) {
      CF_EXPECT(SendTouchEvent(device_label, slot.x, slot.y, down));
    }
    return {};
  }
  auto& ts = ts_it->second;
  CF_EXPECT(ts.SendMultiTouchEvent(slots, down));
  return {};
}

Result<void> EventSinkImpl::SendKeyboardEvent(uint16_t code, bool down) {
  CF_EXPECT(input_devices_.keyboard.has_value(), "No keyboard device setup");
  CF_EXPECT(input_devices_.keyboard->SendEvent(code, down));
  return {};
}

Result<void> EventSinkImpl::SendRotaryEvent(int pixels) {
  CF_EXPECT(input_devices_.rotary.has_value(), "No rotary device setup");
  CF_EXPECT(input_devices_.rotary->SendEvent(pixels));
  return {};
}

Result<void> EventSinkImpl::SendSwitchesEvent(uint16_t code, bool state) {
  CF_EXPECT(input_devices_.switches.has_value(), "No switches device setup");
  CF_EXPECT(input_devices_.switches->SendEvent(code, state));
  return {};
}

class InputConnectorImpl : public InputConnector {
 public:
  InputConnectorImpl() = default;
  ~InputConnectorImpl();

  std::unique_ptr<EventSink> CreateSink() override;

 private:
  InputDevices devices_;
  // Counts the number of events sinks to make sure the class is not destroyed
  // while any of its sinks still exists.
  std::atomic<int> sinks_count_ = 0;
  friend class InputConnectorBuilder;
};

InputConnectorImpl::~InputConnectorImpl() {
  CHECK(sinks_count_ == 0) << "Input connector destroyed with " << sinks_count_
                           << " event sinks left";
}

std::unique_ptr<InputConnector::EventSink> InputConnectorImpl::CreateSink() {
  return std::unique_ptr<InputConnector::EventSink>(
      new EventSinkImpl(devices_, sinks_count_));
}

InputConnectorBuilder::InputConnectorBuilder(InputEventType type)
    : connector_(new InputConnectorImpl()), event_type_(type) {}

InputConnectorBuilder::~InputConnectorBuilder() = default;

void InputConnectorBuilder::WithMultitouchDevice(
    const std::string& device_label, SharedFD server) {
  CHECK(connector_->devices_.multitouch_devices.find(device_label) ==
        connector_->devices_.multitouch_devices.end())
      << "Multiple touch devices with same label: " << device_label;
  connector_->devices_.multitouch_devices.emplace(
      std::piecewise_construct, std::forward_as_tuple(device_label),
      std::forward_as_tuple(NewServerInputConnection(server), event_type_));
}

void InputConnectorBuilder::WithTouchDevice(const std::string& device_label,
                                            SharedFD server) {
  CHECK(connector_->devices_.touch_devices.find(device_label) ==
        connector_->devices_.touch_devices.end())
      << "Multiple touch devices with same label: " << device_label;
  connector_->devices_.touch_devices.emplace(
      std::piecewise_construct, std::forward_as_tuple(device_label),
      std::forward_as_tuple(NewServerInputConnection(server), event_type_));
}

void InputConnectorBuilder::WithKeyboard(SharedFD server) {
  CHECK(!connector_->devices_.keyboard) << "Keyboard already specified";
  connector_->devices_.keyboard.emplace(NewServerInputConnection(server),
                                        event_type_);
}

void InputConnectorBuilder::WithSwitches(SharedFD server) {
  CHECK(!connector_->devices_.switches) << "Switches already specified";
  connector_->devices_.switches.emplace(NewServerInputConnection(server),
                                        event_type_);
}

void InputConnectorBuilder::WithRotary(SharedFD server) {
  CHECK(!connector_->devices_.rotary) << "Rotary already specified";
  connector_->devices_.rotary.emplace(NewServerInputConnection(server),
                                      event_type_);
}

void InputConnectorBuilder::WithMouse(SharedFD server) {
  CHECK(!connector_->devices_.mouse) << "Mouse already specified";
  connector_->devices_.mouse.emplace(NewServerInputConnection(server),
                                     event_type_);
}

std::unique_ptr<InputConnector> InputConnectorBuilder::Build() && {
  return std::move(connector_);
}

}  // namespace cuttlefish

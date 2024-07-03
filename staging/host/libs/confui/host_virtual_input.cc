/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0f
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/libs/confui/host_virtual_input.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace confui {

HostVirtualInput::HostVirtualInput(HostServer& host_server,
                                   HostModeCtrl& host_mode_ctrl,
                                   InputConnector& android_mode_input)
    : host_server_(host_server),
      host_mode_ctrl_(host_mode_ctrl),
      android_mode_input_(android_mode_input) {}

void HostVirtualInput::UserAbortEvent() { host_server_.UserAbortEvent(); }

bool HostVirtualInput::IsConfUiActive() {
  return host_mode_ctrl_.IsConfirmatioUiMode();
}

class HostVirtualInputEventSink : public InputConnector::EventSink {
 public:
  HostVirtualInputEventSink(std::unique_ptr<EventSink> android_mode_input,
                            HostVirtualInput& host_virtual_input)
      : android_mode_input_(std::move(android_mode_input)),
        host_virtual_input_(host_virtual_input) {}

  // EventSink implementation
  Result<void> SendTouchEvent(const std::string& device_label, int x, int y,
                              bool down) override;
  Result<void> SendMultiTouchEvent(const std::string& device_label,
                                   const std::vector<MultitouchSlot>& slots,
                                   bool down) override;
  Result<void> SendKeyboardEvent(uint16_t code, bool down) override;
  Result<void> SendRotaryEvent(int pixels) override;
  Result<void> SendSwitchesEvent(uint16_t code, bool state) override;

 private:
  std::unique_ptr<EventSink> android_mode_input_;
  HostVirtualInput& host_virtual_input_;
};

Result<void> HostVirtualInputEventSink::SendTouchEvent(
    const std::string& device_label, int x, int y, bool down) {
  if (!host_virtual_input_.IsConfUiActive()) {
    return android_mode_input_->SendTouchEvent(device_label, x, y, down);
  }

  if (down) {
    ConfUiLog(INFO) << "TouchEvent occurs in Confirmation UI Mode at [" << x
                    << ", " << y << "]";
    host_virtual_input_.host_server().TouchEvent(x, y, down);
  }
  return {};
}

Result<void> HostVirtualInputEventSink::SendMultiTouchEvent(
    const std::string& device_label, const std::vector<MultitouchSlot>& slots,
    bool down) {
  if (!host_virtual_input_.IsConfUiActive()) {
    CF_EXPECT(
        android_mode_input_->SendMultiTouchEvent(device_label, slots, down));
    return {};
  }
  for (auto& slot : slots) {
    if (down) {
      auto this_x = slot.x;
      auto this_y = slot.y;
      ConfUiLog(INFO) << "TouchEvent occurs in Confirmation UI Mode at ["
                      << this_x << ", " << this_y << "]";
      host_virtual_input_.host_server().TouchEvent(this_x, this_y, down);
    }
  }
  return {};
}

Result<void> HostVirtualInputEventSink::SendKeyboardEvent(uint16_t code,
                                                          bool down) {
  if (!host_virtual_input_.IsConfUiActive()) {
    CF_EXPECT(android_mode_input_->SendKeyboardEvent(code, down));
    return {};
  }
  ConfUiLog(VERBOSE) << "keyboard event ignored in confirmation UI mode";
  return {};
}

Result<void> HostVirtualInputEventSink::SendRotaryEvent(int pixels) {
  if (!host_virtual_input_.IsConfUiActive()) {
    CF_EXPECT(android_mode_input_->SendRotaryEvent(pixels));
    return {};
  }
  ConfUiLog(VERBOSE) << "rotary event ignored in confirmation UI mode";
  return {};
}

Result<void> HostVirtualInputEventSink::SendSwitchesEvent(uint16_t code,
                                                          bool state) {
  if (!host_virtual_input_.IsConfUiActive()) {
    CF_EXPECT(android_mode_input_->SendSwitchesEvent(code, state));
    return {};
  }
  ConfUiLog(VERBOSE) << "switches event ignored in confirmation UI mode";
  return {};
}

std::unique_ptr<InputConnector::EventSink> HostVirtualInput::CreateSink() {
  return std::unique_ptr<EventSink>(
      new HostVirtualInputEventSink(android_mode_input_.CreateSink(), *this));
}

}  // namespace confui
}  // namespace cuttlefish

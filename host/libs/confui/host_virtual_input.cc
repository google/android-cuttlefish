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
    : host_server_(host_server), host_mode_ctrl_(host_mode_ctrl), android_mode_input_(android_mode_input) {}

void HostVirtualInput::UserAbortEvent() { host_server_.UserAbortEvent(); }

bool HostVirtualInput::IsConfUiActive() {
  return host_mode_ctrl_.IsConfirmatioUiMode();
}

Result<void> HostVirtualInput::SendTouchEvent(const std::string& display, int x,
                                              int y, bool down) {
  if (!IsConfUiActive()) {
    return android_mode_input_.SendTouchEvent(display, x, y, down);
  }

  if (down) {
    ConfUiLog(INFO) << "TouchEvent occurs in Confirmation UI Mode at [" << x
                    << ", " << y << "]";
    host_server_.TouchEvent(x, y, down);
  }
  return {};
}

Result<void> HostVirtualInput::SendMultiTouchEvent(
    const std::string& display_label, const std::vector<MultitouchSlot>& slots,
    bool down) {
  if (!IsConfUiActive()) {
    return android_mode_input_.SendMultiTouchEvent(display_label, slots, down);
  }
  for (auto& slot: slots) {
    if (down) {
      auto this_x = slot.x;
      auto this_y = slot.y;
      ConfUiLog(INFO) << "TouchEvent occurs in Confirmation UI Mode at ["
                      << this_x << ", " << this_y << "]";
      host_server_.TouchEvent(this_x, this_y, down);
    }
  }
  return {};
}

Result<void> HostVirtualInput::SendKeyboardEvent(uint16_t code, bool down) {
  if (!IsConfUiActive()) {
    return android_mode_input_.SendKeyboardEvent(code, down);
  }
  ConfUiLog(VERBOSE) << "keyboard event ignored in confirmation UI mode";
  return {};
}

Result<void> HostVirtualInput::SendRotaryEvent(int pixels) {
  if (!IsConfUiActive()) {
    return android_mode_input_.SendRotaryEvent(pixels);
  }
  ConfUiLog(VERBOSE) << "rotary event ignored in confirmation UI mode";
  return {};
}

Result<void> HostVirtualInput::SendSwitchesEvent(uint16_t code, bool state) {
  if (!IsConfUiActive()) {
    return android_mode_input_.SendSwitchesEvent(code, state);
  }
  ConfUiLog(VERBOSE) << "switches event ignored in confirmation UI mode";
  return {};
}

}  // namespace confui
}  // namespace cuttlefish

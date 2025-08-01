/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/frontend/webrtc/libdevice/gamepad.h"

#include <linux/input.h>

#include <array>

// This map represents the association between the index code from Javascript
// Gamepad API and the linux input event code.
// The Javascript Gamepad API index code is defined in:
// https://w3c.github.io/gamepad/#gamepad-button-mapping
// The linux input event code is defined in:
// https://elixir.bootlin.com/linux/latest/source/include/uapi/linux/input-event-codes.h
static const std::array<uint16_t, 17> kJsIndexToLinuxMapping = {
    BTN_SOUTH, BTN_EAST, BTN_WEST,   BTN_NORTH, BTN_TL,     BTN_TR,
    0,         0,        BTN_SELECT, BTN_START, BTN_THUMBL, BTN_THUMBR,
    KEY_UP,    KEY_DOWN, KEY_LEFT,   KEY_RIGHT, BTN_MODE};

uint16_t JsIndexToLinux(const int32_t& index_code) {
  if (index_code < 0 || index_code >= kJsIndexToLinuxMapping.size()) {
    return 0;
  }
  return kJsIndexToLinuxMapping[index_code];
}


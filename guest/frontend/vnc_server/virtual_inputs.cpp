/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "virtual_inputs.h"
#include <mutex>

using cvd::vnc::VirtualInputs;

void VirtualInputs::GenerateKeyPressEvent(int code, bool down) {
  std::lock_guard<std::mutex> guard(m_);
  virtual_keyboard_.GenerateKeyPressEvent(code, down);
}

void VirtualInputs::PressPowerButton(bool down) {
  std::lock_guard<std::mutex> guard(m_);
  virtual_power_button_.HandleButtonPressEvent(down);
}

void VirtualInputs::HandlePointerEvent(bool touch_down, int x, int y) {
  std::lock_guard<std::mutex> guard(m_);
  virtual_touch_pad_.HandlePointerEvent(touch_down, x, y);
}

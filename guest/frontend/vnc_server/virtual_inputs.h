#pragma once
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

#include "VirtualInputDevice.h"
#include "vnc_utils.h"

#include <linux/input.h>
#include <android-base/thread_annotations.h>

#include <mutex>

namespace cvd {
namespace vnc {

class VirtualInputs {
 public:
  void GenerateKeyPressEvent(int code, bool down);
  void PressPowerButton(bool down);
  void HandlePointerEvent(bool touch_down, int x, int y);

 private:
  std::mutex m_;
  VirtualKeyboard virtual_keyboard_ GUARDED_BY(m_){"remote-keyboard"};
  VirtualTouchPad virtual_touch_pad_ GUARDED_BY(m_){
      "remote-touchpad", ActualScreenWidth(), ActualScreenHeight()};
  VirtualButton virtual_power_button_ GUARDED_BY(m_){"remote-power", KEY_POWER};
};

}  // namespace vnc
}  // namespace cvd

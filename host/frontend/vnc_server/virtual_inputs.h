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

#include <linux/input.h>

#include <mutex>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/threads/thread_annotations.h"
#include "host/frontend/vnc_server/virtual_input_device.h"
#include "host/frontend/vnc_server/vnc_utils.h"

namespace avd {
namespace vnc {

class VirtualInputs {
 public:
  VirtualInputs();
  ~VirtualInputs();

  void GenerateKeyPressEvent(int code, bool down);
  void PressPowerButton(bool down);
  void HandlePointerEvent(bool touch_down, int x, int y);

 private:
  avd::SharedFD monkey_socket_;
  bool SendMonkeyComand(std::string cmd);
  std::mutex m_;
  VirtualKeyboard virtual_keyboard_ GUARDED_BY(m_);
  VirtualTouchPad virtual_touch_pad_ GUARDED_BY(m_);
  VirtualButton virtual_power_button_ GUARDED_BY(m_);
};

}  // namespace vnc
}  // namespace avd

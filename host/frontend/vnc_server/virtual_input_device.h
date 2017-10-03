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

#include <functional>
#include <map>
#include <string>

namespace avd {

// Base virtual input device class which contains a bunch of boiler-plate code.
class VirtualInputDevice {
 public:
  VirtualInputDevice(std::function<bool(std::string)> cmd_sender)
      : send_command_(cmd_sender) {}

 protected:
  bool SendCommand(std::string cmd) { return send_command_(cmd); }

 private:
  std::function<bool(std::string)> send_command_;
};

// Virtual touch-pad.
class VirtualTouchPad : public VirtualInputDevice {
 public:
  VirtualTouchPad(std::function<bool(std::string)> cmd_sender)
      : VirtualInputDevice(cmd_sender) {}

  void HandlePointerEvent(bool touch_down, int x, int y);

 private:
  std::string GetCommand(bool touch_down, int x, int y);
  bool prev_touch_ = false;
  int prev_x_ = -1;
  int prev_y_ = -1;
};

// Virtual button.
class VirtualButton : public VirtualInputDevice {
 public:
  VirtualButton(std::string input_keycode,
                std::function<bool(std::string)> cmd_sender)
      : VirtualInputDevice(cmd_sender), input_keycode_(input_keycode) {}

  void HandleButtonPressEvent(bool button_down);

 private:
  std::string input_keycode_;
};

// Virtual keyboard.
class VirtualKeyboard : public VirtualInputDevice {
 public:
  VirtualKeyboard(std::function<bool(std::string)> cmd_sender);
  virtual ~VirtualKeyboard() {}

  void GenerateKeyPressEvent(int code, bool down);

 private:
  std::map<uint32_t, std::string> keymapping_;
};

}  // namespace avd

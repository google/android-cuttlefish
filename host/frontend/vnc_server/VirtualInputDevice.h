/*
 * Copyright (C) 2014 The Android Open Source Project
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

#ifndef VIRTUAL_INPUT_DEVICE_H_
#define VIRTUAL_INPUT_DEVICE_H_

#include <linux/uinput.h>
#include <map>

namespace avd {
// Base virtual input device class which contains a bunch of boiler-plate code.
class VirtualInputDevice {
 public:
  VirtualInputDevice(const char* name, uint16_t bus_type, uint16_t vendor,
                     uint16_t product, uint16_t version);
  virtual ~VirtualInputDevice();

 protected:
  bool Init(uint32_t* events, int num_events, uint32_t* keys, int num_keys,
            uint32_t* abs, int num_abs, uint32_t* props, int num_props);

  bool EmitEvent(uint16_t type, uint16_t code, uint32_t value);

  struct uinput_user_dev* uinput_user_dev() {
    return &uinput_user_dev_;
  }

 private:
  bool EnableEventBits(uint32_t* events, int num_elements);
  bool EnableKeyBits(uint32_t* keys, int num_elements);
  bool EnableAbsBits(uint32_t* abs, int num_elements);
  bool EnablePropBits(uint32_t* props, int num_elements);
  bool DoIoctls(int request, uint32_t* list, int num_elements);
  bool FinalizeDeviceCreation();

  int fd_;
  struct uinput_user_dev uinput_user_dev_;
};

// Virtual touch-pad.
class VirtualTouchPad : public VirtualInputDevice {
 public:
  VirtualTouchPad(const char* name, int x_res, int y_res);
  virtual ~VirtualTouchPad() {}

  void HandlePointerEvent(bool touch_down, int x, int y);

 private:
  static uint32_t Senabled_events_[];
  static uint32_t Senabled_keys_[];
  static uint32_t Senabled_abs_[];
  static uint32_t Senabled_props_[];

  int x_res_;
  int y_res_;
};

// Virtual button.
class VirtualButton : public VirtualInputDevice {
 public:
  VirtualButton(const char* name, uint32_t input_keycode);
  virtual ~VirtualButton() {}

  void HandleButtonPressEvent(bool button_down);

 private:
  static uint32_t Senabled_events_[];
  uint32_t input_keycode_;
};

// Virtual keyboard.
class VirtualKeyboard : public VirtualInputDevice {
 public:
  VirtualKeyboard(const char* name);
  virtual ~VirtualKeyboard() {}

  void GenerateKeyPressEvent(int code, bool down);

 private:
  static uint32_t Senabled_events_[];
  std::map<uint32_t, uint32_t> keymapping_;
};

}  // namespace avd
#endif

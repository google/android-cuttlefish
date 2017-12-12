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

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include "keysyms.h"

#define LOG_TAG "RemoterVirtualInput"
#include <cutils/log.h>
#include <cutils/properties.h>

#include "VirtualInputDevice.h"

#define ARRAY_SIZE(a)           \
  ((sizeof(a) / sizeof(*(a))) / \
   static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))

//////////////////////////
// VirtualButton Support
//////////////////////////

namespace avd {
uint32_t VirtualButton::Senabled_events_[] = {EV_KEY};

VirtualButton::VirtualButton(const char* name, uint32_t input_keycode)
    : VirtualInputDevice(name, BUS_USB, 0x6006, 0x6007, 1),
      input_keycode_(input_keycode) {
  if (!VirtualInputDevice::Init(Senabled_events_, ARRAY_SIZE(Senabled_events_),
                                &input_keycode_, 1, NULL, 0, NULL, 0)) {
    LOG_FATAL("VirtualInputDevice Init() failed");
  }
}

void VirtualButton::HandleButtonPressEvent(bool button_down) {
  EmitEvent(EV_KEY, input_keycode_, button_down);
  EmitEvent(EV_SYN, 0, 0);
}

//////////////////////////
// VirtualKeyboard Support
//////////////////////////

uint32_t VirtualKeyboard::Senabled_events_[] = {EV_KEY};

struct KeyEventToInput {
  uint32_t xk;
  uint32_t input_code;
};

static const KeyEventToInput key_table[] = {
    {xk::AltLeft, KEY_LEFTALT},
    {xk::ControlLeft, KEY_LEFTCTRL},
    {xk::ShiftLeft, KEY_LEFTSHIFT},
    {xk::AltRight, KEY_RIGHTALT},
    {xk::ControlRight, KEY_RIGHTCTRL},
    {xk::ShiftRight, KEY_RIGHTSHIFT},
    {xk::MetaLeft, KEY_LEFTMETA},
    {xk::MetaRight, KEY_RIGHTMETA},
    {xk::MultiKey, KEY_COMPOSE},

    {xk::CapsLock, KEY_CAPSLOCK},
    {xk::NumLock, KEY_NUMLOCK},
    {xk::ScrollLock, KEY_SCROLLLOCK},

    {xk::BackSpace, KEY_BACKSPACE},
    {xk::Tab, KEY_TAB},
    {xk::Return, KEY_ENTER},
    {xk::Escape, KEY_ESC},

    {' ', KEY_SPACE},
    {'!', KEY_1},
    {'"', KEY_APOSTROPHE},
    {'#', KEY_3},
    {'$', KEY_4},
    {'%', KEY_5},
    {'^', KEY_6},
    {'&', KEY_7},
    {'\'', KEY_APOSTROPHE},
    {'(', KEY_9},
    {')', KEY_0},
    {'*', KEY_8},
    {'+', KEY_EQUAL},
    {',', KEY_COMMA},
    {'-', KEY_MINUS},
    {'.', KEY_DOT},
    {'/', KEY_SLASH},
    {'0', KEY_0},
    {'1', KEY_1},
    {'2', KEY_2},
    {'3', KEY_3},
    {'4', KEY_4},
    {'5', KEY_5},
    {'6', KEY_6},
    {'7', KEY_7},
    {'8', KEY_8},
    {'9', KEY_9},
    {':', KEY_SEMICOLON},
    {';', KEY_SEMICOLON},
    {'<', KEY_COMMA},
    {'=', KEY_EQUAL},
    {'>', KEY_DOT},
    {'?', KEY_SLASH},
    {'@', KEY_2},
    {'A', KEY_A},
    {'B', KEY_B},
    {'C', KEY_C},
    {'D', KEY_D},
    {'E', KEY_E},
    {'F', KEY_F},
    {'G', KEY_G},
    {'H', KEY_H},
    {'I', KEY_I},
    {'J', KEY_J},
    {'K', KEY_K},
    {'L', KEY_L},
    {'M', KEY_M},
    {'N', KEY_N},
    {'O', KEY_O},
    {'P', KEY_P},
    {'Q', KEY_Q},
    {'R', KEY_R},
    {'S', KEY_S},
    {'T', KEY_T},
    {'U', KEY_U},
    {'V', KEY_V},
    {'W', KEY_W},
    {'X', KEY_X},
    {'Y', KEY_Y},
    {'Z', KEY_Z},
    {'[', KEY_LEFTBRACE},
    {'\\', KEY_BACKSLASH},
    {']', KEY_RIGHTBRACE},
    {'-', KEY_MINUS},
    {'_', KEY_MINUS},
    {'`', KEY_GRAVE},
    {'a', KEY_A},
    {'b', KEY_B},
    {'c', KEY_C},
    {'d', KEY_D},
    {'e', KEY_E},
    {'f', KEY_F},
    {'g', KEY_G},
    {'h', KEY_H},
    {'i', KEY_I},
    {'j', KEY_J},
    {'k', KEY_K},
    {'l', KEY_L},
    {'m', KEY_M},
    {'n', KEY_N},
    {'o', KEY_O},
    {'p', KEY_P},
    {'q', KEY_Q},
    {'r', KEY_R},
    {'s', KEY_S},
    {'t', KEY_T},
    {'u', KEY_U},
    {'v', KEY_V},
    {'w', KEY_W},
    {'x', KEY_X},
    {'y', KEY_Y},
    {'z', KEY_Z},
    {'{', KEY_LEFTBRACE},
    {'\\', KEY_BACKSLASH},
    {'|', KEY_BACKSLASH},
    {'}', KEY_RIGHTBRACE},
    {'~', KEY_GRAVE},

    {xk::F1, KEY_F1},
    {xk::F2, KEY_F2},
    {xk::F3, KEY_F3},
    {xk::F4, KEY_F4},
    {xk::F5, KEY_F5},
    {xk::F6, KEY_F6},
    {xk::F7, KEY_F7},
    {xk::F8, KEY_F8},
    {xk::F9, KEY_F9},
    {xk::F10, KEY_F10},
    {xk::F11, KEY_F11},
    {xk::F12, KEY_F12},
    {xk::F13, KEY_F13},
    {xk::F14, KEY_F14},
    {xk::F15, KEY_F15},
    {xk::F16, KEY_F16},
    {xk::F17, KEY_F17},
    {xk::F18, KEY_F18},
    {xk::F19, KEY_F19},
    {xk::F20, KEY_F20},
    {xk::F21, KEY_F21},
    {xk::F22, KEY_F22},
    {xk::F23, KEY_F23},
    {xk::F24, KEY_F24},

    {xk::Keypad0, KEY_KP0},
    {xk::Keypad1, KEY_KP1},
    {xk::Keypad2, KEY_KP2},
    {xk::Keypad3, KEY_KP3},
    {xk::Keypad4, KEY_KP4},
    {xk::Keypad5, KEY_KP5},
    {xk::Keypad6, KEY_KP6},
    {xk::Keypad7, KEY_KP7},
    {xk::Keypad8, KEY_KP8},
    {xk::Keypad9, KEY_KP9},
    {xk::KeypadMultiply, KEY_KPASTERISK},
    {xk::KeypadSubtract, KEY_KPMINUS},
    {xk::KeypadAdd, KEY_KPPLUS},
    {xk::KeypadDecimal, KEY_KPDOT},
    {xk::KeypadEnter, KEY_KPENTER},
    {xk::KeypadDivide, KEY_KPSLASH},
    {xk::KeypadEqual, KEY_KPEQUAL},
    {xk::PlusMinus, KEY_KPPLUSMINUS},

    {xk::SysReq, KEY_SYSRQ},
    {xk::LineFeed, KEY_LINEFEED},
    {xk::Home, KEY_HOME},
    {xk::Up, KEY_UP},
    {xk::PageUp, KEY_PAGEUP},
    {xk::Left, KEY_LEFT},
    {xk::Right, KEY_RIGHT},
    {xk::End, KEY_END},
    {xk::Down, KEY_DOWN},
    {xk::PageDown, KEY_PAGEDOWN},
    {xk::Insert, KEY_INSERT},
    {xk::Delete, KEY_DELETE},
    {xk::Pause, KEY_PAUSE},
    {xk::KeypadSeparator, KEY_KPCOMMA},
    {xk::Yen, KEY_YEN},
    {xk::Cancel, KEY_STOP},
    {xk::Redo, KEY_AGAIN},
    {xk::Undo, KEY_UNDO},
    {xk::Find, KEY_FIND},
    {xk::Print, KEY_PRINT},
    {xk::VolumeDown, KEY_VOLUMEDOWN},
    {xk::Mute, KEY_MUTE},
    {xk::VolumeUp, KEY_VOLUMEUP},
    {xk::Menu, KEY_MENU},
    {xk::VNCMenu, KEY_MENU},
};

VirtualKeyboard::VirtualKeyboard(const char* name)
    : VirtualInputDevice(name, BUS_USB, 0x6006, 0x6008, 1) {
  std::vector<uint32_t> keycodes(ARRAY_SIZE(key_table));
  for (size_t i = 0; i < keycodes.size(); ++i) {
    keymapping_[key_table[i].xk] = key_table[i].input_code;
    keycodes[i] = key_table[i].input_code;
  }

  if (!VirtualInputDevice::Init(Senabled_events_, ARRAY_SIZE(Senabled_events_),
                                &keycodes[0], keycodes.size(), NULL, 0, NULL,
                                0)) {
    LOG_FATAL("VirtualInputDevice Init() failed");
  }
}

void VirtualKeyboard::GenerateKeyPressEvent(int keycode, bool button_down) {
  if (keymapping_.count(keycode)) {
    EmitEvent(EV_KEY, keymapping_[keycode], button_down);
    EmitEvent(EV_SYN, 0, 0);
  }
  ALOGI("Unknown keycode %d", keycode);
}

//////////////////////////
// VirtualTouchPad Support
//////////////////////////

uint32_t VirtualTouchPad::Senabled_events_[] = {EV_ABS, EV_KEY, EV_SYN};
uint32_t VirtualTouchPad::Senabled_keys_[] = {BTN_TOUCH};
uint32_t VirtualTouchPad::Senabled_abs_[] = {ABS_X, ABS_Y};
uint32_t VirtualTouchPad::Senabled_props_[] = {INPUT_PROP_DIRECT};

VirtualTouchPad::VirtualTouchPad(const char* name, int x_res, int y_res)
    : VirtualInputDevice(name, BUS_USB, 0x6006, 0x6006, 1),
      x_res_(x_res),
      y_res_(y_res) {
  // Customization of uinput_user_dev() must happen before calling our base
  // Init().
  uinput_user_dev()->absmin[ABS_X] = 0;
  uinput_user_dev()->absmax[ABS_X] = x_res_;
  uinput_user_dev()->absmin[ABS_Y] = 0;
  uinput_user_dev()->absmax[ABS_Y] = y_res_;

  if (!VirtualInputDevice::Init(Senabled_events_, ARRAY_SIZE(Senabled_events_),
                                Senabled_keys_, ARRAY_SIZE(Senabled_keys_),
                                Senabled_abs_, ARRAY_SIZE(Senabled_abs_),
                                Senabled_props_, ARRAY_SIZE(Senabled_props_))) {
    LOG_FATAL("VirtualInputDevice Init() failed");
  }
}

void VirtualTouchPad::HandlePointerEvent(bool touch_down, int x, int y) {
  EmitEvent(EV_ABS, ABS_X, x);
  EmitEvent(EV_ABS, ABS_Y, y);
  EmitEvent(EV_KEY, BTN_TOUCH, touch_down);
  EmitEvent(EV_SYN, 0, 0);
}

//////////////////////////////////
// Base VirtualInputDevice Support
//////////////////////////////////
VirtualInputDevice::VirtualInputDevice(const char* name, uint16_t bus_type,
                                       uint16_t vendor, uint16_t product,
                                       uint16_t version)
    : fd_(-1) {
  memset(&uinput_user_dev_, 0, sizeof(uinput_user_dev_));
  strncpy(uinput_user_dev_.name, name, sizeof(uinput_user_dev_.name));
  uinput_user_dev_.id.bustype = bus_type;
  uinput_user_dev_.id.vendor = vendor;
  uinput_user_dev_.id.product = product;
  uinput_user_dev_.id.version = version;
}

VirtualInputDevice::~VirtualInputDevice() {
  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }
}

bool VirtualInputDevice::Init(uint32_t* events, int num_events, uint32_t* keys,
                              int num_keys, uint32_t* abs, int num_abs,
                              uint32_t* props, int num_props) {
  if ((fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK)) < 0) {
    SLOGE("Failed to open /dev/uinput (%s)", strerror(errno));
    return false;
  }
  if (events && !EnableEventBits(events, num_events)) {
    SLOGE("Failed to set event bits (%s)", strerror(errno));
    return false;
  }
  if (keys && !EnableKeyBits(keys, num_keys)) {
    SLOGE("Failed to set key bits (%s)", strerror(errno));
    return false;
  }
  if (abs && !EnableAbsBits(abs, num_abs)) {
    SLOGE("Failed to set abs bits (%s)", strerror(errno));
    return false;
  }
  if (props && !EnablePropBits(props, num_props)) {
    SLOGE("Failed to set prop bits (%s)", strerror(errno));
    return false;
  }
  if (!FinalizeDeviceCreation()) {
    SLOGE("Failed to finalize device creation (%s)", strerror(errno));
    return false;
  }
  return true;
}

bool VirtualInputDevice::EmitEvent(uint16_t type, uint16_t code,
                                   uint32_t value) {
  struct input_event ev;
  ev.type = type;
  ev.code = code;
  ev.value = value;
  if (write(fd_, &ev, sizeof(ev)) < 0) {
    SLOGE("write() failed (%s)", strerror(errno));
    return false;
  }
  return true;
}

bool VirtualInputDevice::DoIoctls(int request, uint32_t* list,
                                  int num_elements) {
  for (int i = 0; i < num_elements; i++) {
    int rc = ioctl(fd_, request, *list++);
    if (rc < 0) {
      SLOGE("ioctl failed (%s)", strerror(errno));
      return false;
    }
  }
  return true;
}

bool VirtualInputDevice::EnableEventBits(uint32_t* events, int num_elements) {
  return DoIoctls(UI_SET_EVBIT, events, num_elements);
}

bool VirtualInputDevice::EnableKeyBits(uint32_t* keys, int num_elements) {
  return DoIoctls(UI_SET_KEYBIT, keys, num_elements);
}

bool VirtualInputDevice::EnableAbsBits(uint32_t* abs, int num_elements) {
  return DoIoctls(UI_SET_ABSBIT, abs, num_elements);
}

bool VirtualInputDevice::EnablePropBits(uint32_t* props, int num_elements) {
// JB and ICE do not have the latest uinput headers.
#ifndef UI_SET_PROPBIT
#define UI_SET_PROPBIT _IOW(UINPUT_IOCTL_BASE, 110, int)
#endif  // #ifndef UI_SET_PROPBIT
  return DoIoctls(UI_SET_PROPBIT, props, num_elements);
}

bool VirtualInputDevice::FinalizeDeviceCreation() {
  if (write(fd_, &uinput_user_dev_, sizeof(uinput_user_dev_)) < 0) {
    SLOGE("Unable to set input device info (%s)", strerror(errno));
    return false;
  }
  if (ioctl(fd_, UI_DEV_CREATE) < 0) {
    SLOGE("Unable to create input device (%s)", strerror(errno));
    return false;
  }
  return true;
}

}  // namespace avd

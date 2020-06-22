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

#include "virtual_keyboard.h"

namespace cuttlefish_input_service {

VirtualKeyboard::VirtualKeyboard()
    : VirtualDeviceBase("VSoC keyboard", 0x6008) {}

const std::vector<const uint32_t>& VirtualKeyboard::GetEventTypes() const {
  static const std::vector<const uint32_t> evt_types{EV_KEY};
  return evt_types;
}
const std::vector<const uint32_t>& VirtualKeyboard::GetKeys() const {
  static const std::vector<const uint32_t> keys{
      KEY_0,           KEY_1,          KEY_2,          KEY_3,
      KEY_4,           KEY_5,          KEY_6,          KEY_7,
      KEY_8,           KEY_9,          KEY_A,          KEY_AGAIN,
      KEY_APOSTROPHE,  KEY_B,          KEY_BACKSLASH,  KEY_BACKSPACE,
      KEY_C,           KEY_CAPSLOCK,   KEY_COMMA,      KEY_COMPOSE,
      KEY_D,           KEY_DELETE,     KEY_DOT,        KEY_DOWN,
      KEY_E,           KEY_END,        KEY_ENTER,      KEY_EQUAL,
      KEY_ESC,         KEY_F,          KEY_F1,         KEY_F10,
      KEY_F11,         KEY_F12,        KEY_F13,        KEY_F14,
      KEY_F15,         KEY_F16,        KEY_F17,        KEY_F18,
      KEY_F19,         KEY_F2,         KEY_F20,        KEY_F21,
      KEY_F22,         KEY_F23,        KEY_F24,        KEY_F3,
      KEY_F4,          KEY_F5,         KEY_F6,         KEY_F7,
      KEY_F8,          KEY_F9,         KEY_FIND,       KEY_G,
      KEY_GRAVE,       KEY_H,          KEY_HOME,       KEY_I,
      KEY_INSERT,      KEY_J,          KEY_K,          KEY_KP0,
      KEY_KP1,         KEY_KP2,        KEY_KP3,        KEY_KP4,
      KEY_KP5,         KEY_KP6,        KEY_KP7,        KEY_KP8,
      KEY_KP9,         KEY_KPASTERISK, KEY_KPCOMMA,    KEY_KPDOT,
      KEY_KPENTER,     KEY_KPEQUAL,    KEY_KPMINUS,    KEY_KPPLUS,
      KEY_KPPLUSMINUS, KEY_KPSLASH,    KEY_L,          KEY_LEFT,
      KEY_LEFTALT,     KEY_LEFTBRACE,  KEY_LEFTCTRL,   KEY_LEFTMETA,
      KEY_LEFTSHIFT,   KEY_LINEFEED,   KEY_M,          KEY_MENU,
      KEY_MINUS,       KEY_MUTE,       KEY_N,          KEY_NUMLOCK,
      KEY_O,           KEY_P,          KEY_PAGEDOWN,   KEY_PAGEUP,
      KEY_PAUSE,       KEY_PRINT,      KEY_Q,          KEY_R,
      KEY_RIGHT,       KEY_RIGHTALT,   KEY_RIGHTBRACE, KEY_RIGHTCTRL,
      KEY_RIGHTMETA,   KEY_RIGHTSHIFT, KEY_S,          KEY_SCROLLLOCK,
      KEY_SEMICOLON,   KEY_SLASH,      KEY_SPACE,      KEY_STOP,
      KEY_SYSRQ,       KEY_T,          KEY_TAB,        KEY_U,
      KEY_UNDO,        KEY_UP,         KEY_V,          KEY_VOLUMEDOWN,
      KEY_VOLUMEUP,    KEY_W,          KEY_X,          KEY_Y,
      KEY_YEN,         KEY_Z};
  return keys;
}

}  // namespace cuttlefish_input_service

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

#include "host/frontend/vnc_server/virtual_inputs.h"
#include <gflags/gflags.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <mutex>
#include "keysyms.h"

using cvd::vnc::VirtualInputs;
using vsoc::input_events::InputEventsRegionView;

namespace {
void AddKeyMappings(std::map<uint32_t, uint32_t>* key_mapping) {
  (*key_mapping)[cvd::xk::AltLeft] = KEY_LEFTALT;
  (*key_mapping)[cvd::xk::ControlLeft] = KEY_LEFTCTRL;
  (*key_mapping)[cvd::xk::ShiftLeft] = KEY_LEFTSHIFT;
  (*key_mapping)[cvd::xk::AltRight] = KEY_RIGHTALT;
  (*key_mapping)[cvd::xk::ControlRight] = KEY_RIGHTCTRL;
  (*key_mapping)[cvd::xk::ShiftRight] = KEY_RIGHTSHIFT;
  (*key_mapping)[cvd::xk::MetaLeft] = KEY_LEFTMETA;
  (*key_mapping)[cvd::xk::MetaRight] = KEY_RIGHTMETA;
  (*key_mapping)[cvd::xk::MultiKey] = KEY_COMPOSE;

  (*key_mapping)[cvd::xk::CapsLock] = KEY_CAPSLOCK;
  (*key_mapping)[cvd::xk::NumLock] = KEY_NUMLOCK;
  (*key_mapping)[cvd::xk::ScrollLock] = KEY_SCROLLLOCK;

  (*key_mapping)[cvd::xk::BackSpace] = KEY_BACKSPACE;
  (*key_mapping)[cvd::xk::Tab] = KEY_TAB;
  (*key_mapping)[cvd::xk::Return] = KEY_ENTER;
  (*key_mapping)[cvd::xk::Escape] = KEY_ESC;

  (*key_mapping)[' '] = KEY_SPACE;
  (*key_mapping)['!'] = KEY_1;
  (*key_mapping)['"'] = KEY_APOSTROPHE;
  (*key_mapping)['#'] = KEY_3;
  (*key_mapping)['$'] = KEY_4;
  (*key_mapping)['%'] = KEY_5;
  (*key_mapping)['^'] = KEY_6;
  (*key_mapping)['&'] = KEY_7;
  (*key_mapping)['\''] = KEY_APOSTROPHE;
  (*key_mapping)['('] = KEY_9;
  (*key_mapping)[')'] = KEY_0;
  (*key_mapping)['*'] = KEY_8;
  (*key_mapping)['+'] = KEY_EQUAL;
  (*key_mapping)[','] = KEY_COMMA;
  (*key_mapping)['-'] = KEY_MINUS;
  (*key_mapping)['.'] = KEY_DOT;
  (*key_mapping)['/'] = KEY_SLASH;
  (*key_mapping)['0'] = KEY_0;
  (*key_mapping)['1'] = KEY_1;
  (*key_mapping)['2'] = KEY_2;
  (*key_mapping)['3'] = KEY_3;
  (*key_mapping)['4'] = KEY_4;
  (*key_mapping)['5'] = KEY_5;
  (*key_mapping)['6'] = KEY_6;
  (*key_mapping)['7'] = KEY_7;
  (*key_mapping)['8'] = KEY_8;
  (*key_mapping)['9'] = KEY_9;
  (*key_mapping)[':'] = KEY_SEMICOLON;
  (*key_mapping)[';'] = KEY_SEMICOLON;
  (*key_mapping)['<'] = KEY_COMMA;
  (*key_mapping)['='] = KEY_EQUAL;
  (*key_mapping)['>'] = KEY_DOT;
  (*key_mapping)['?'] = KEY_SLASH;
  (*key_mapping)['@'] = KEY_2;
  (*key_mapping)['A'] = KEY_A;
  (*key_mapping)['B'] = KEY_B;
  (*key_mapping)['C'] = KEY_C;
  (*key_mapping)['D'] = KEY_D;
  (*key_mapping)['E'] = KEY_E;
  (*key_mapping)['F'] = KEY_F;
  (*key_mapping)['G'] = KEY_G;
  (*key_mapping)['H'] = KEY_H;
  (*key_mapping)['I'] = KEY_I;
  (*key_mapping)['J'] = KEY_J;
  (*key_mapping)['K'] = KEY_K;
  (*key_mapping)['L'] = KEY_L;
  (*key_mapping)['M'] = KEY_M;
  (*key_mapping)['N'] = KEY_N;
  (*key_mapping)['O'] = KEY_O;
  (*key_mapping)['P'] = KEY_P;
  (*key_mapping)['Q'] = KEY_Q;
  (*key_mapping)['R'] = KEY_R;
  (*key_mapping)['S'] = KEY_S;
  (*key_mapping)['T'] = KEY_T;
  (*key_mapping)['U'] = KEY_U;
  (*key_mapping)['V'] = KEY_V;
  (*key_mapping)['W'] = KEY_W;
  (*key_mapping)['X'] = KEY_X;
  (*key_mapping)['Y'] = KEY_Y;
  (*key_mapping)['Z'] = KEY_Z;
  (*key_mapping)['['] = KEY_LEFTBRACE;
  (*key_mapping)['\\'] = KEY_BACKSLASH;
  (*key_mapping)[']'] = KEY_RIGHTBRACE;
  (*key_mapping)['-'] = KEY_MINUS;
  (*key_mapping)['_'] = KEY_MINUS;
  (*key_mapping)['`'] = KEY_GRAVE;
  (*key_mapping)['a'] = KEY_A;
  (*key_mapping)['b'] = KEY_B;
  (*key_mapping)['c'] = KEY_C;
  (*key_mapping)['d'] = KEY_D;
  (*key_mapping)['e'] = KEY_E;
  (*key_mapping)['f'] = KEY_F;
  (*key_mapping)['g'] = KEY_G;
  (*key_mapping)['h'] = KEY_H;
  (*key_mapping)['i'] = KEY_I;
  (*key_mapping)['j'] = KEY_J;
  (*key_mapping)['k'] = KEY_K;
  (*key_mapping)['l'] = KEY_L;
  (*key_mapping)['m'] = KEY_M;
  (*key_mapping)['n'] = KEY_N;
  (*key_mapping)['o'] = KEY_O;
  (*key_mapping)['p'] = KEY_P;
  (*key_mapping)['q'] = KEY_Q;
  (*key_mapping)['r'] = KEY_R;
  (*key_mapping)['s'] = KEY_S;
  (*key_mapping)['t'] = KEY_T;
  (*key_mapping)['u'] = KEY_U;
  (*key_mapping)['v'] = KEY_V;
  (*key_mapping)['w'] = KEY_W;
  (*key_mapping)['x'] = KEY_X;
  (*key_mapping)['y'] = KEY_Y;
  (*key_mapping)['z'] = KEY_Z;
  (*key_mapping)['{'] = KEY_LEFTBRACE;
  (*key_mapping)['\\'] = KEY_BACKSLASH;
  (*key_mapping)['|'] = KEY_BACKSLASH;
  (*key_mapping)['}'] = KEY_RIGHTBRACE;
  (*key_mapping)['~'] = KEY_GRAVE;

  (*key_mapping)[cvd::xk::F1] = KEY_F1;
  (*key_mapping)[cvd::xk::F2] = KEY_F2;
  (*key_mapping)[cvd::xk::F3] = KEY_F3;
  (*key_mapping)[cvd::xk::F4] = KEY_F4;
  (*key_mapping)[cvd::xk::F5] = KEY_F5;
  (*key_mapping)[cvd::xk::F6] = KEY_F6;
  (*key_mapping)[cvd::xk::F7] = KEY_F7;
  (*key_mapping)[cvd::xk::F8] = KEY_F8;
  (*key_mapping)[cvd::xk::F9] = KEY_F9;
  (*key_mapping)[cvd::xk::F10] = KEY_F10;
  (*key_mapping)[cvd::xk::F11] = KEY_F11;
  (*key_mapping)[cvd::xk::F12] = KEY_F12;
  (*key_mapping)[cvd::xk::F13] = KEY_F13;
  (*key_mapping)[cvd::xk::F14] = KEY_F14;
  (*key_mapping)[cvd::xk::F15] = KEY_F15;
  (*key_mapping)[cvd::xk::F16] = KEY_F16;
  (*key_mapping)[cvd::xk::F17] = KEY_F17;
  (*key_mapping)[cvd::xk::F18] = KEY_F18;
  (*key_mapping)[cvd::xk::F19] = KEY_F19;
  (*key_mapping)[cvd::xk::F20] = KEY_F20;
  (*key_mapping)[cvd::xk::F21] = KEY_F21;
  (*key_mapping)[cvd::xk::F22] = KEY_F22;
  (*key_mapping)[cvd::xk::F23] = KEY_F23;
  (*key_mapping)[cvd::xk::F24] = KEY_F24;

  (*key_mapping)[cvd::xk::Keypad0] = KEY_KP0;
  (*key_mapping)[cvd::xk::Keypad1] = KEY_KP1;
  (*key_mapping)[cvd::xk::Keypad2] = KEY_KP2;
  (*key_mapping)[cvd::xk::Keypad3] = KEY_KP3;
  (*key_mapping)[cvd::xk::Keypad4] = KEY_KP4;
  (*key_mapping)[cvd::xk::Keypad5] = KEY_KP5;
  (*key_mapping)[cvd::xk::Keypad6] = KEY_KP6;
  (*key_mapping)[cvd::xk::Keypad7] = KEY_KP7;
  (*key_mapping)[cvd::xk::Keypad8] = KEY_KP8;
  (*key_mapping)[cvd::xk::Keypad9] = KEY_KP9;
  (*key_mapping)[cvd::xk::KeypadMultiply] = KEY_KPASTERISK;
  (*key_mapping)[cvd::xk::KeypadSubtract] = KEY_KPMINUS;
  (*key_mapping)[cvd::xk::KeypadAdd] = KEY_KPPLUS;
  (*key_mapping)[cvd::xk::KeypadDecimal] = KEY_KPDOT;
  (*key_mapping)[cvd::xk::KeypadEnter] = KEY_KPENTER;
  (*key_mapping)[cvd::xk::KeypadDivide] = KEY_KPSLASH;
  (*key_mapping)[cvd::xk::KeypadEqual] = KEY_KPEQUAL;
  (*key_mapping)[cvd::xk::PlusMinus] = KEY_KPPLUSMINUS;

  (*key_mapping)[cvd::xk::SysReq] = KEY_SYSRQ;
  (*key_mapping)[cvd::xk::LineFeed] = KEY_LINEFEED;
  (*key_mapping)[cvd::xk::Home] = KEY_HOME;
  (*key_mapping)[cvd::xk::Up] = KEY_UP;
  (*key_mapping)[cvd::xk::PageUp] = KEY_PAGEUP;
  (*key_mapping)[cvd::xk::Left] = KEY_LEFT;
  (*key_mapping)[cvd::xk::Right] = KEY_RIGHT;
  (*key_mapping)[cvd::xk::End] = KEY_END;
  (*key_mapping)[cvd::xk::Down] = KEY_DOWN;
  (*key_mapping)[cvd::xk::PageDown] = KEY_PAGEDOWN;
  (*key_mapping)[cvd::xk::Insert] = KEY_INSERT;
  (*key_mapping)[cvd::xk::Delete] = KEY_DELETE;
  (*key_mapping)[cvd::xk::Pause] = KEY_PAUSE;
  (*key_mapping)[cvd::xk::KeypadSeparator] = KEY_KPCOMMA;
  (*key_mapping)[cvd::xk::Yen] = KEY_YEN;
  (*key_mapping)[cvd::xk::Cancel] = KEY_STOP;
  (*key_mapping)[cvd::xk::Redo] = KEY_AGAIN;
  (*key_mapping)[cvd::xk::Undo] = KEY_UNDO;
  (*key_mapping)[cvd::xk::Find] = KEY_FIND;
  (*key_mapping)[cvd::xk::Print] = KEY_PRINT;
  (*key_mapping)[cvd::xk::VolumeDown] = KEY_VOLUMEDOWN;
  (*key_mapping)[cvd::xk::Mute] = KEY_MUTE;
  (*key_mapping)[cvd::xk::VolumeUp] = KEY_VOLUMEUP;
  (*key_mapping)[cvd::xk::Menu] = KEY_MENU;
  (*key_mapping)[cvd::xk::VNCMenu] = KEY_MENU;
}
}  // namespace

VirtualInputs::VirtualInputs()
  : input_events_region_view_{
      vsoc::input_events::InputEventsRegionView::GetInstance(
          vsoc::GetDomain().c_str())} {
  if (!input_events_region_view_) {
    LOG(FATAL) << "Failed to open Input events region view";
  }
  AddKeyMappings(&keymapping_);
}

void VirtualInputs::GenerateKeyPressEvent(int key_code, bool down) {
  if (keymapping_.count(key_code)) {
    input_events_region_view_->HandleKeyboardEvent(down, keymapping_[key_code]);
  } else {
    LOG(INFO) << "Unknown keycode" << key_code;
  }
}

void VirtualInputs::PressPowerButton(bool down) {
  input_events_region_view_->HandlePowerButtonEvent(down);
}

void VirtualInputs::HandlePointerEvent(bool touch_down, int x, int y) {
  input_events_region_view_->HandleSingleTouchEvent(touch_down, x, y);
}

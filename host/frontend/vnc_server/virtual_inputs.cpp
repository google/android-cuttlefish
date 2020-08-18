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
#include <android-base/logging.h>
#include <gflags/gflags.h>
#include <linux/input.h>

#include <cstdint>
#include "keysyms.h"

#include <host/libs/input_connectors/input_connectors.h>

using cuttlefish::vnc::VirtualInputs;

DEFINE_int32(touch_fd, -1,
             "A fd for a socket where to accept touch connections");

DEFINE_int32(keyboard_fd, -1,
             "A fd for a socket where to accept keyboard connections");

DEFINE_bool(write_virtio_input, false,
            "Whether to write the virtio_input struct over the socket");

namespace {

void AddKeyMappings(std::map<uint32_t, uint16_t>* key_mapping) {
  (*key_mapping)[cuttlefish::xk::AltLeft] = KEY_LEFTALT;
  (*key_mapping)[cuttlefish::xk::ControlLeft] = KEY_LEFTCTRL;
  (*key_mapping)[cuttlefish::xk::ShiftLeft] = KEY_LEFTSHIFT;
  (*key_mapping)[cuttlefish::xk::AltRight] = KEY_RIGHTALT;
  (*key_mapping)[cuttlefish::xk::ControlRight] = KEY_RIGHTCTRL;
  (*key_mapping)[cuttlefish::xk::ShiftRight] = KEY_RIGHTSHIFT;
  (*key_mapping)[cuttlefish::xk::MetaLeft] = KEY_LEFTMETA;
  (*key_mapping)[cuttlefish::xk::MetaRight] = KEY_RIGHTMETA;
  (*key_mapping)[cuttlefish::xk::MultiKey] = KEY_COMPOSE;

  (*key_mapping)[cuttlefish::xk::CapsLock] = KEY_CAPSLOCK;
  (*key_mapping)[cuttlefish::xk::NumLock] = KEY_NUMLOCK;
  (*key_mapping)[cuttlefish::xk::ScrollLock] = KEY_SCROLLLOCK;

  (*key_mapping)[cuttlefish::xk::BackSpace] = KEY_BACKSPACE;
  (*key_mapping)[cuttlefish::xk::Tab] = KEY_TAB;
  (*key_mapping)[cuttlefish::xk::Return] = KEY_ENTER;
  (*key_mapping)[cuttlefish::xk::Escape] = KEY_ESC;

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

  (*key_mapping)[cuttlefish::xk::F1] = KEY_F1;
  (*key_mapping)[cuttlefish::xk::F2] = KEY_F2;
  (*key_mapping)[cuttlefish::xk::F3] = KEY_F3;
  (*key_mapping)[cuttlefish::xk::F4] = KEY_F4;
  (*key_mapping)[cuttlefish::xk::F5] = KEY_F5;
  (*key_mapping)[cuttlefish::xk::F6] = KEY_F6;
  (*key_mapping)[cuttlefish::xk::F7] = KEY_F7;
  (*key_mapping)[cuttlefish::xk::F8] = KEY_F8;
  (*key_mapping)[cuttlefish::xk::F9] = KEY_F9;
  (*key_mapping)[cuttlefish::xk::F10] = KEY_F10;
  (*key_mapping)[cuttlefish::xk::F11] = KEY_F11;
  (*key_mapping)[cuttlefish::xk::F12] = KEY_F12;
  (*key_mapping)[cuttlefish::xk::F13] = KEY_F13;
  (*key_mapping)[cuttlefish::xk::F14] = KEY_F14;
  (*key_mapping)[cuttlefish::xk::F15] = KEY_F15;
  (*key_mapping)[cuttlefish::xk::F16] = KEY_F16;
  (*key_mapping)[cuttlefish::xk::F17] = KEY_F17;
  (*key_mapping)[cuttlefish::xk::F18] = KEY_F18;
  (*key_mapping)[cuttlefish::xk::F19] = KEY_F19;
  (*key_mapping)[cuttlefish::xk::F20] = KEY_F20;
  (*key_mapping)[cuttlefish::xk::F21] = KEY_F21;
  (*key_mapping)[cuttlefish::xk::F22] = KEY_F22;
  (*key_mapping)[cuttlefish::xk::F23] = KEY_F23;
  (*key_mapping)[cuttlefish::xk::F24] = KEY_F24;

  (*key_mapping)[cuttlefish::xk::Keypad0] = KEY_KP0;
  (*key_mapping)[cuttlefish::xk::Keypad1] = KEY_KP1;
  (*key_mapping)[cuttlefish::xk::Keypad2] = KEY_KP2;
  (*key_mapping)[cuttlefish::xk::Keypad3] = KEY_KP3;
  (*key_mapping)[cuttlefish::xk::Keypad4] = KEY_KP4;
  (*key_mapping)[cuttlefish::xk::Keypad5] = KEY_KP5;
  (*key_mapping)[cuttlefish::xk::Keypad6] = KEY_KP6;
  (*key_mapping)[cuttlefish::xk::Keypad7] = KEY_KP7;
  (*key_mapping)[cuttlefish::xk::Keypad8] = KEY_KP8;
  (*key_mapping)[cuttlefish::xk::Keypad9] = KEY_KP9;
  (*key_mapping)[cuttlefish::xk::KeypadMultiply] = KEY_KPASTERISK;
  (*key_mapping)[cuttlefish::xk::KeypadSubtract] = KEY_KPMINUS;
  (*key_mapping)[cuttlefish::xk::KeypadAdd] = KEY_KPPLUS;
  (*key_mapping)[cuttlefish::xk::KeypadDecimal] = KEY_KPDOT;
  (*key_mapping)[cuttlefish::xk::KeypadEnter] = KEY_KPENTER;
  (*key_mapping)[cuttlefish::xk::KeypadDivide] = KEY_KPSLASH;
  (*key_mapping)[cuttlefish::xk::KeypadEqual] = KEY_KPEQUAL;
  (*key_mapping)[cuttlefish::xk::PlusMinus] = KEY_KPPLUSMINUS;

  (*key_mapping)[cuttlefish::xk::SysReq] = KEY_SYSRQ;
  (*key_mapping)[cuttlefish::xk::LineFeed] = KEY_LINEFEED;
  (*key_mapping)[cuttlefish::xk::Home] = KEY_HOME;
  (*key_mapping)[cuttlefish::xk::Up] = KEY_UP;
  (*key_mapping)[cuttlefish::xk::PageUp] = KEY_PAGEUP;
  (*key_mapping)[cuttlefish::xk::Left] = KEY_LEFT;
  (*key_mapping)[cuttlefish::xk::Right] = KEY_RIGHT;
  (*key_mapping)[cuttlefish::xk::End] = KEY_END;
  (*key_mapping)[cuttlefish::xk::Down] = KEY_DOWN;
  (*key_mapping)[cuttlefish::xk::PageDown] = KEY_PAGEDOWN;
  (*key_mapping)[cuttlefish::xk::Insert] = KEY_INSERT;
  (*key_mapping)[cuttlefish::xk::Delete] = KEY_DELETE;
  (*key_mapping)[cuttlefish::xk::Pause] = KEY_PAUSE;
  (*key_mapping)[cuttlefish::xk::KeypadSeparator] = KEY_KPCOMMA;
  (*key_mapping)[cuttlefish::xk::Yen] = KEY_YEN;
  (*key_mapping)[cuttlefish::xk::Cancel] = KEY_STOP;
  (*key_mapping)[cuttlefish::xk::Redo] = KEY_AGAIN;
  (*key_mapping)[cuttlefish::xk::Undo] = KEY_UNDO;
  (*key_mapping)[cuttlefish::xk::Find] = KEY_FIND;
  (*key_mapping)[cuttlefish::xk::Print] = KEY_PRINT;
  (*key_mapping)[cuttlefish::xk::VolumeDown] = KEY_VOLUMEDOWN;
  (*key_mapping)[cuttlefish::xk::Mute] = KEY_MUTE;
  (*key_mapping)[cuttlefish::xk::VolumeUp] = KEY_VOLUMEUP;
  (*key_mapping)[cuttlefish::xk::Menu] = KEY_MENU;
  (*key_mapping)[cuttlefish::xk::VNCMenu] = KEY_MENU;
}

}  // namespace

class SocketVirtualInputs : public VirtualInputs {
 public:
  SocketVirtualInputs(
      std::unique_ptr<cuttlefish::TouchConnector> touch_connector,
      std::unique_ptr<cuttlefish::KeyboardConnector> keyboard_connector)
      : touch_connector_(std::move(touch_connector)),
        keyboard_connector_(std::move(keyboard_connector)) {}

  void GenerateKeyPressEvent(int key_code, bool down) override {
    keyboard_connector_->InjectKeyEvent(keymapping_[key_code], down);
  }

  void PressPowerButton(bool down) override {
    keyboard_connector_->InjectKeyEvent(keymapping_[KEY_POWER], down);
  }

  void HandlePointerEvent(bool touch_down, int x, int y) override {
    // TODO(b/124121375): Use multitouch when available
    touch_connector_->InjectTouchEvent(x, y, touch_down);
  }

 private:
  std::unique_ptr<cuttlefish::TouchConnector> touch_connector_;
  std::unique_ptr<cuttlefish::KeyboardConnector> keyboard_connector_;
};

VirtualInputs::VirtualInputs() { AddKeyMappings(&keymapping_); }

VirtualInputs* VirtualInputs::Get() {
  auto touch_fd = cuttlefish::SharedFD::DupAndClose(FLAGS_touch_fd);
  CHECK(touch_fd->IsOpen()) << "Failed to dup touch fd: " << FLAGS_touch_fd;
  auto keyboard_fd = cuttlefish::SharedFD::DupAndClose(FLAGS_keyboard_fd);
  CHECK(keyboard_fd->IsOpen())
      << "Failed to dup keyboard fd: " << FLAGS_keyboard_fd;

  return new SocketVirtualInputs(
      cuttlefish::TouchConnector::Create(touch_fd, FLAGS_write_virtio_input),
      cuttlefish::KeyboardConnector::Create(keyboard_fd,
                                            FLAGS_write_virtio_input));
}

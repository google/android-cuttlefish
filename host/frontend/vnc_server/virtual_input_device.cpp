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

#include "host/frontend/vnc_server/virtual_input_device.h"

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

#include <glog/logging.h>
#include "host/frontend/vnc_server/keysyms.h"

namespace avd {

//////////////////////////
// VirtualButton Support
//////////////////////////

void VirtualButton::HandleButtonPressEvent(bool button_down) {
  SendCommand(std::string("key ") + "down " + input_keycode_ + "\n");
}

//////////////////////////
// VirtualKeyboard Support
//////////////////////////

struct KeyEventToInput {
  uint32_t xk;
  std::string input_code;
};

static const KeyEventToInput key_table[] = {
    {xk::AltLeft, "KEYCODE_ALT_LEFT"},
    {xk::ControlLeft, "KEYCODE_CTRL_LEFT"},
    {xk::ShiftLeft, "KEYCODE_SHIFT_LEFT"},
    {xk::AltRight, "KEYCODE_ALT_RIGHT"},
    {xk::ControlRight, "KEYCODE_CTRL_RIGHT"},
    {xk::ShiftRight, "KEYCODE_SHIFT_RIGHT"},
    {xk::MetaLeft, "KEYCODE_META_LEFT"},
    {xk::MetaRight, "KEYCODE_META_RIGHT"},
    // {xk::MultiKey, "KEYCODE_COMPOSE"},

    {xk::CapsLock, "KEYCODE_CAPS_LOCK"},
    {xk::NumLock, "KEYCODE_NUM_LOCK"},
    {xk::ScrollLock, "KEYCODE_SCROLL_LOCK"},

    {xk::BackSpace, "KEYCODE_DEL"},
    {xk::Tab, "KEYCODE_TAB"},
    {xk::Return, "KEYCODE_ENTER"},
    {xk::Escape, "KEYCODE_ESCAPE"},

    {' ', "KEYCODE_SPACE"},
    {'!', "KEYCODE_1"},
    {'"', "KEYCODE_APOSTROPHE"},
    {'#', "KEYCODE_POUND"},
    {'$', "KEYCODE_4"},
    {'%', "KEYCODE_5"},
    {'^', "KEYCODE_6"},
    {'&', "KEYCODE_7"},
    {'\'', "KEYCODE_APOSTROPHE"},
    {'(', "KEYCODE_NUMPAD_LEFT_PAREN"},
    {')', "KEYCODE_NUMPAD_RIGHT_PAREN"},
    {'*', "KEYCODE_STAR"},
    {'+', "KEYCODE_EQUALS"},
    {',', "KEYCODE_COMMA"},
    {'-', "KEYCODE_MINUS"},
    {'.', "KEYCODE_PERIOD"},
    {'/', "KEYCODE_SLASH"},
    {'0', "KEYCODE_0"},
    {'1', "KEYCODE_1"},
    {'2', "KEYCODE_2"},
    {'3', "KEYCODE_3"},
    {'4', "KEYCODE_4"},
    {'5', "KEYCODE_5"},
    {'6', "KEYCODE_6"},
    {'7', "KEYCODE_7"},
    {'8', "KEYCODE_8"},
    {'9', "KEYCODE_9"},
    {':', "KEYCODE_SEMICOLON"},
    {';', "KEYCODE_SEMICOLON"},
    {'<', "KEYCODE_COMMA"},
    {'=', "KEYCODE_EQUALS"},
    {'>', "KEYCODE_PERIOD"},
    {'?', "KEYCODE_SLASH"},
    {'@', "KEYCODE_2"},
    {'A', "KEYCODE_A"},
    {'B', "KEYCODE_B"},
    {'C', "KEYCODE_C"},
    {'D', "KEYCODE_D"},
    {'E', "KEYCODE_E"},
    {'F', "KEYCODE_F"},
    {'G', "KEYCODE_G"},
    {'H', "KEYCODE_H"},
    {'I', "KEYCODE_I"},
    {'J', "KEYCODE_J"},
    {'K', "KEYCODE_K"},
    {'L', "KEYCODE_L"},
    {'M', "KEYCODE_M"},
    {'N', "KEYCODE_N"},
    {'O', "KEYCODE_O"},
    {'P', "KEYCODE_P"},
    {'Q', "KEYCODE_Q"},
    {'R', "KEYCODE_R"},
    {'S', "KEYCODE_S"},
    {'T', "KEYCODE_T"},
    {'U', "KEYCODE_U"},
    {'V', "KEYCODE_V"},
    {'W', "KEYCODE_W"},
    {'X', "KEYCODE_X"},
    {'Y', "KEYCODE_Y"},
    {'Z', "KEYCODE_Z"},
    {'[', "KEYCODE_LEFT_BRACKET"},
    {'\\', "KEYCODE_BACKSLASH"},
    {']', "KEYCODE_RIGHT_BRACKET"},
    {'-', "KEYCODE_MINUS"},
    {'_', "KEYCODE_MINUS"},
    {'`', "KEYCODE_GRAVE"},
    {'a', "KEYCODE_A"},
    {'b', "KEYCODE_B"},
    {'c', "KEYCODE_C"},
    {'d', "KEYCODE_D"},
    {'e', "KEYCODE_E"},
    {'f', "KEYCODE_F"},
    {'g', "KEYCODE_G"},
    {'h', "KEYCODE_H"},
    {'i', "KEYCODE_I"},
    {'j', "KEYCODE_J"},
    {'k', "KEYCODE_K"},
    {'l', "KEYCODE_L"},
    {'m', "KEYCODE_M"},
    {'n', "KEYCODE_N"},
    {'o', "KEYCODE_O"},
    {'p', "KEYCODE_P"},
    {'q', "KEYCODE_Q"},
    {'r', "KEYCODE_R"},
    {'s', "KEYCODE_S"},
    {'t', "KEYCODE_T"},
    {'u', "KEYCODE_U"},
    {'v', "KEYCODE_V"},
    {'w', "KEYCODE_W"},
    {'x', "KEYCODE_X"},
    {'y', "KEYCODE_Y"},
    {'z', "KEYCODE_Z"},
    {'{', "KEYCODE_LEFT_BRACKET"},
    {'\\', "KEYCODE_BACKSLASH"},
    // {'|', "|"},
    {'}', "KEYCODE_RIGHT_BRACKET"},
    {'~', "KEYCODE_GRAVE"},

    {xk::F1, "KEYCODE_F1"},
    {xk::F2, "KEYCODE_F2"},
    {xk::F3, "KEYCODE_F3"},
    {xk::F4, "KEYCODE_F4"},
    {xk::F5, "KEYCODE_F5"},
    {xk::F6, "KEYCODE_F6"},
    {xk::F7, "KEYCODE_F7"},
    {xk::F8, "KEYCODE_F8"},
    {xk::F9, "KEYCODE_F9"},
    {xk::F10, "KEYCODE_F10"},
    {xk::F11, "KEYCODE_F11"},
    {xk::F12, "KEYCODE_F12"},
    // {xk::F13, "KEYCODE_F13"},
    // {xk::F14, "KEYCODE_F14"},
    // {xk::F15, "KEYCODE_F15"},
    // {xk::F16, "KEYCODE_F16"},
    // {xk::F17, "KEYCODE_F17"},
    // {xk::F18, "KEYCODE_F18"},
    // {xk::F19, "KEYCODE_F19"},
    // {xk::F20, "KEYCODE_F20"},
    // {xk::F21, "KEYCODE_F21"},
    // {xk::F22, "KEYCODE_F22"},
    // {xk::F23, "KEYCODE_F23"},
    // {xk::F24, "KEYCODE_F24"},

    {xk::Keypad0, "KEYCODE_NUMPAD_0"},
    {xk::Keypad1, "KEYCODE_NUMPAD_1"},
    {xk::Keypad2, "KEYCODE_NUMPAD_2"},
    {xk::Keypad3, "KEYCODE_NUMPAD_3"},
    {xk::Keypad4, "KEYCODE_NUMPAD_4"},
    {xk::Keypad5, "KEYCODE_NUMPAD_5"},
    {xk::Keypad6, "KEYCODE_NUMPAD_6"},
    {xk::Keypad7, "KEYCODE_NUMPAD_7"},
    {xk::Keypad8, "KEYCODE_NUMPAD_8"},
    {xk::Keypad9, "KEYCODE_NUMPAD_9"},
    {xk::KeypadMultiply, "KEYCODE_NUMPAD_MULTIPLY"},
    {xk::KeypadSubtract, "KEYCODE_NUMPAD_SUBTRACT"},
    {xk::KeypadAdd, "KEYCODE_NUMPAD_ADD"},
    {xk::KeypadDecimal, "KEYCODE_NUMPAD_DOT"},
    {xk::KeypadEnter, "KEYCODE_NUMPAD_ENTER"},
    {xk::KeypadDivide, "KEYCODE_NUMPAD_DIVIDE"},
    {xk::KeypadEqual, "KEYCODE_NUMPAD_EQUALS"},
    // {xk::PlusMinus, "KEYCODE_NUMPAD_PLUSMINUS"},

    {xk::SysReq, "KEYCODE_SYSRQ"},
    // {xk::LineFeed, "KEYCODE_LINEFEED"},
    {xk::Home, "KEYCODE_HOME"},
    {xk::Up, "KEYCODE_DPAD_UP"},
    {xk::PageUp, "KEYCODE_PAGE_UP"},
    {xk::Left, "KEYCODE_DPAD_LEFT"},
    {xk::Right, "KEYCODE_DPAD_RIGHT"},
    {xk::End, "KEYCODE_MOVE_END"},
    {xk::Down, "KEYCODE_DPAD_DOWN"},
    {xk::PageDown, "KEYCODE_PAGE_DOWN"},
    {xk::Insert, "KEYCODE_INSERT"},
    {xk::Delete, "KEYCODE_FORWARD_DEL"},
    {xk::Pause, "KEYCODE_BREAK"},
    {xk::KeypadSeparator, "KEYCODE_NUMPAD_COMMA"},
    {xk::Yen, "KEYCODE_YEN"},
    // {xk::Cancel, "KEYCODE_STOP"},
    // {xk::Redo, "KEYCODE_AGAIN"},
    // {xk::Undo, "KEYCODE_UNDO"},
    // {xk::Find, "KEYCODE_FIND"},
    // {xk::Print, "KEYCODE_PRINT"},
    {xk::VolumeDown, "KEYCODE_VOLUME_DOWN"},
    {xk::Mute, "KEYCODE_MUTE"},
    {xk::VolumeUp, "KEYCODE_VOLUME_UP"},
    {xk::Menu, "KEYCODE_MENU"},
    {xk::VNCMenu, "KEYCODE_MENU"},
};

VirtualKeyboard::VirtualKeyboard(std::function<bool(std::string)> cmd_sender)
    : VirtualInputDevice(cmd_sender) {
  for (const auto& key : key_table) {
    keymapping_[key.xk] = key.input_code;
  }
}

void VirtualKeyboard::GenerateKeyPressEvent(int keycode, bool button_down) {
  if (keymapping_.count(keycode)) {
    SendCommand(std::string("key ") + (button_down ? "down " : "up ") +
                keymapping_[keycode] + "\n");
  } else {
    LOG(INFO) << "Unknown keycode " << keycode;
  }
}

//////////////////////////
// VirtualTouchPad Support
//////////////////////////

std::string VirtualTouchPad::GetCommand(bool touch_down, int x, int y) {
  std::string cmd;
  if (touch_down == prev_touch_) {
    if (touch_down && (x != prev_x_ || y != prev_y_)) {
      cmd = "touch move ";
    }  // else don't repeat last event or send non touch mouse movements
  } else if (touch_down) {
    cmd = "touch down ";
  } else {
    cmd = "touch up ";
  }
  prev_touch_ = touch_down;
  prev_x_ = x;
  prev_y_ = y;
  return cmd;
}

void VirtualTouchPad::HandlePointerEvent(bool touch_down, int x, int y) {
  SendCommand(GetCommand(touch_down, x, y) + std::to_string(x) + " " +
              std::to_string(y) + "\n");
}

}  // namespace avd

/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <webrtc/Keyboard.h>

#include <linux/input.h>

#include <map>

static const std::map<std::string, uint16_t> kDomToLinuxMapping = {
    {"Backquote", KEY_GRAVE},
    {"Backslash", KEY_BACKSLASH},
    {"Backspace", KEY_BACKSPACE},
    {"BracketLeft", KEY_LEFTBRACE},
    {"BracketRight", KEY_RIGHTBRACE},
    {"Comma", KEY_COMMA},
    {"Digit0", KEY_0},
    {"Digit1", KEY_1},
    {"Digit2", KEY_2},
    {"Digit3", KEY_3},
    {"Digit4", KEY_4},
    {"Digit5", KEY_5},
    {"Digit6", KEY_6},
    {"Digit7", KEY_7},
    {"Digit8", KEY_8},
    {"Digit9", KEY_9},
    {"Equal", KEY_EQUAL},
    {"IntlBackslash", KEY_BACKSLASH},
    {"IntlRo", KEY_RO},
    {"IntlYen", KEY_BACKSLASH},
    {"KeyA", KEY_A},
    {"KeyB", KEY_B},
    {"KeyC", KEY_C},
    {"KeyD", KEY_D},
    {"KeyE", KEY_E},
    {"KeyF", KEY_F},
    {"KeyG", KEY_G},
    {"KeyH", KEY_H},
    {"KeyI", KEY_I},
    {"KeyJ", KEY_J},
    {"KeyK", KEY_K},
    {"KeyL", KEY_L},
    {"KeyM", KEY_M},
    {"KeyN", KEY_N},
    {"KeyO", KEY_O},
    {"KeyP", KEY_P},
    {"KeyQ", KEY_Q},
    {"KeyR", KEY_R},
    {"KeyS", KEY_S},
    {"KeyT", KEY_T},
    {"KeyU", KEY_U},
    {"KeyV", KEY_V},
    {"KeyW", KEY_W},
    {"KeyX", KEY_X},
    {"KeyY", KEY_Y},
    {"KeyZ", KEY_Z},
    {"Minus", KEY_MINUS},
    {"Period", KEY_DOT},
    {"Quote", KEY_APOSTROPHE},
    {"Semicolon", KEY_SEMICOLON},
    {"Slash", KEY_SLASH},
    {"AltLeft", KEY_LEFTALT},
    {"AltRight", KEY_RIGHTALT},
    {"CapsLock", KEY_CAPSLOCK},
    {"ContextMenu", KEY_CONTEXT_MENU},
    {"ControlLeft", KEY_LEFTCTRL},
    {"ControlRight", KEY_RIGHTCTRL},
    {"Enter", KEY_ENTER},
    {"MetaLeft", KEY_LEFTMETA},
    {"MetaRight", KEY_RIGHTMETA},
    {"ShiftLeft", KEY_LEFTSHIFT},
    {"ShiftRight", KEY_RIGHTSHIFT},
    {"Space", KEY_SPACE},
    {"Tab", KEY_TAB},
    {"Delete", KEY_DELETE},
    {"End", KEY_END},
    {"Help", KEY_HELP},
    {"Home", KEY_HOME},
    {"Insert", KEY_INSERT},
    {"PageDown", KEY_PAGEDOWN},
    {"PageUp", KEY_PAGEUP},
    {"ArrowDown", KEY_DOWN},
    {"ArrowLeft", KEY_LEFT},
    {"ArrowRight", KEY_RIGHT},
    {"ArrowUp", KEY_UP},

    {"NumLock", KEY_NUMLOCK},
    {"Numpad0", KEY_KP0},
    {"Numpad1", KEY_KP1},
    {"Numpad2", KEY_KP2},
    {"Numpad3", KEY_KP3},
    {"Numpad4", KEY_KP4},
    {"Numpad5", KEY_KP5},
    {"Numpad6", KEY_KP6},
    {"Numpad7", KEY_KP7},
    {"Numpad8", KEY_KP8},
    {"Numpad9", KEY_KP9},
    {"NumpadAdd", KEY_KPPLUS},
    {"NumpadBackspace", KEY_BACKSPACE},
    {"NumpadClear", KEY_CLEAR},
    {"NumpadComma", KEY_KPCOMMA},
    {"NumpadDecimal", KEY_KPDOT},
    {"NumpadDivide", KEY_KPSLASH},
    {"NumpadEnter", KEY_KPENTER},
    {"NumpadEqual", KEY_KPEQUAL},
    /*
    {"NumpadClearEntry", },
    {"NumpadHash", },
    {"NumpadMemoryAdd", },
    {"NumpadMemoryClear", },
    {"NumpadMemoryRecall", },
    {"NumpadMemoryStore", },
    {"NumpadMemorySubtract", },
    */
    {"NumpadMultiply", KEY_KPASTERISK},
    {"NumpadParenLeft", KEY_KPLEFTPAREN},
    {"NumpadParenRight", KEY_KPRIGHTPAREN},
    {"NumpadStar", KEY_KPASTERISK},
    {"NumpadSubtract", KEY_KPMINUS},

    {"Escape", KEY_ESC},
    {"F1", KEY_F1},
    {"F2", KEY_F2},
    {"F3", KEY_F3},
    {"F4", KEY_F4},
    {"F5", KEY_F5},
    {"F6", KEY_F6},
    {"F7", KEY_F7},
    {"F8", KEY_F8},
    {"F9", KEY_F9},
    {"F10", KEY_F10},
    {"F11", KEY_F11},
    {"F12", KEY_F12},
    {"Fn", KEY_FN},
    /*{"FnLock", },*/
    {"PrintScreen", KEY_SYSRQ},
    {"ScrollLock", KEY_SCROLLLOCK},
    {"Pause", KEY_PAUSE}};

uint16_t DomKeyCodeToLinux(const std::string& dom_KEY_code) {
  const auto it = kDomToLinuxMapping.find(dom_KEY_code);
  if (it == kDomToLinuxMapping.end()) {
    return 0;
  }
  return it->second;
}

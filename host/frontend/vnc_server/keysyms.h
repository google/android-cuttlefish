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

#include <cstdint>

namespace cuttlefish {
namespace xk {

constexpr uint32_t BackSpace = 0xff08, Tab = 0xff09, Return = 0xff0d,
                   Enter = Return, Escape = 0xff1b, MultiKey = 0xff20,
                   Insert = 0xff63, Delete = 0xffff, Pause = 0xff13,
                   Home = 0xff50, End = 0xff57, PageUp = 0xff55,
                   PageDown = 0xff56, Left = 0xff51, Up = 0xff52,
                   Right = 0xff53, Down = 0xff54, F1 = 0xffbe, F2 = 0xffbf,
                   F3 = 0xffc0, F4 = 0xffc1, F5 = 0xffc2, F6 = 0xffc3,
                   F7 = 0xffc4, F8 = 0xffc5, F9 = 0xffc6, F10 = 0xffc7,
                   F11 = 0xffc8, F12 = 0xffc9, F13 = 0xffca, F14 = 0xffcb,
                   F15 = 0xffcc, F16 = 0xffcd, F17 = 0xffce, F18 = 0xffcf,
                   F19 = 0xffd0, F20 = 0xffd1, F21 = 0xffd2, F22 = 0xffd3,
                   F23 = 0xffd4, F24 = 0xffd5, ShiftLeft = 0xffe1,
                   ShiftRight = 0xffe2, ControlLeft = 0xffe3,
                   ControlRight = 0xffe4, MetaLeft = 0xffe7, MetaRight = 0xffe8,
                   AltLeft = 0xffe9, AltRight = 0xffea, CapsLock = 0xffe5,
                   NumLock = 0xff7f, ScrollLock = 0xff14, Keypad0 = 0xffb0,
                   Keypad1 = 0xffb1, Keypad2 = 0xffb2, Keypad3 = 0xffb3,
                   Keypad4 = 0xffb4, Keypad5 = 0xffb5, Keypad6 = 0xffb6,
                   Keypad7 = 0xffb7, Keypad8 = 0xffb8, Keypad9 = 0xffb9,
                   KeypadMultiply = 0xffaa, KeypadSubtract = 0xffad,
                   KeypadAdd = 0xffab, KeypadDecimal = 0xffae,
                   KeypadEnter = 0xff8d, KeypadDivide = 0xffaf,
                   KeypadEqual = 0xffbd, PlusMinus = 0xb1, SysReq = 0xff15,
                   LineFeed = 0xff0a, KeypadSeparator = 0xffac, Yen = 0xa5,
                   Cancel = 0xff69, Undo = 0xff65, Redo = 0xff66, Find = 0xff68,
                   Print = 0xff61, VolumeDown = 0x1008ff11, Mute = 0x1008ff12,
                   VolumeUp = 0x1008ff13, Menu = 0xff67,
                   VNCMenu = 0xffed;  // VNC seems to translate MENU to this

}  // namespace xk
}  // namespace cuttlefish

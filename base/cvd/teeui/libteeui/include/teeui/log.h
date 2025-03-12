/*
 *
 * Copyright 2019, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBTEEUI_INCLUDE_TEEUI_LOG_H_
#define LIBTEEUI_INCLUDE_TEEUI_LOG_H_

#ifdef TEEUI_DO_LOG_DEBUG
#include <iomanip>
#include <iostream>
#define TEEUI_LOG std::cout
#define ENDL std::endl
#else
#define TEEUI_LOG ::teeui::bits::silencer
#define ENDL 0
#endif

namespace teeui {
namespace bits {

struct silencer_t {};
static silencer_t silencer;

template <typename T> silencer_t& operator<<(silencer_t& out, const T&) {
    return out;
}

}  // namespace bits
}  // namespace teeui

#endif  // LIBTEEUI_INCLUDE_TEEUI_LOG_H_

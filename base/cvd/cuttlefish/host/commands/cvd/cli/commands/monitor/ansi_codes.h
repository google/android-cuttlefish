/*
 * Copyright (C) 2026 The Android Open Source Project
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

#pragma once

#include <string>
#include <string_view>

namespace cuttlefish {

inline constexpr std::string_view kAnsiReset = "\033[0m";
inline constexpr std::string_view kAnsiRed = "\033[0;31m";
inline constexpr std::string_view kAnsiGreen = "\033[0;32m";
inline constexpr std::string_view kAnsiYellow = "\033[0;33m";
inline constexpr std::string_view kAnsiCyan = "\033[0;36m";
inline constexpr std::string_view kAnsiWhite = "\033[0;37m";

inline constexpr std::string_view kAnsiClearScreen = "\033[J";

std::string AnsiCursorUp(int n);

}  // namespace cuttlefish

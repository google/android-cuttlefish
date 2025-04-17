/*
 * Copyright (C) 2015-2022 The Android Open Source Project
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
#include <stdarg.h>

//
// This file defines C and C++ replacements for scanf to parse a string in a
// locale-independent way. This is useful when parsing input data that comes
// not from user, but from some kind of a fixed protocol with predefined locale
// settings.
// Just use these functions as drop-in replacements of sscanf();
//
// Note1: if the input string contains any dot characters other than decimal
// separators, the results of parsing will be screwed: in Windows the
// implementation replaces all dots with the current decimal separator to parse
// using current locale.
// Note2: current implementation only supports parsing floating point numbers -
// no code for monetary values, dates, digit grouping etc.
// The limitation is because of MinGW's lack of per-thread locales support.
//

#ifdef __cplusplus
#include <utility>

extern "C" {
int SscanfWithCLocale(const char* string, const char* format, ...);
}
namespace cuttlefish {

template <class... Args>
int SscanfWithCLocale(const char* string, const char* format, Args... args) {
  return ::SscanfWithCLocale(string, format, std::forward<Args>(args)...);
}

}  // namespace cuttlefish

#endif  // __cplusplus

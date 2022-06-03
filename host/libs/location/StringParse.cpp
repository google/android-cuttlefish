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

#include "StringParse.h"

#include <assert.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cuttlefish {
namespace {

extern "C" int SscanfWithCLocale(const char* string, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const int res = ::vsscanf(string, format, args);
  va_end(args);
  return res;
}
}  // namespace
}  // namespace cuttlefish
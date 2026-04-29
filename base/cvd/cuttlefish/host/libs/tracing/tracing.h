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

#include <cstdint>
#include <string_view>

#include "absl/strings/str_format.h"

namespace cuttlefish {

void TraceEventBegin(std::string_view str);
void TraceEventEnd();

class ScopedTrace {
 public:
  inline ScopedTrace(std::string_view str) { TraceEventBegin(str); }

  ~ScopedTrace() { TraceEventEnd(); }
};

#define CF_SCOPED_TRACE_VAR_NAME2(x, y) x##y
#define CF_SCOPED_TRACE_VAR_NAME(x, y) CF_SCOPED_TRACE_VAR_NAME2(x, y)

#define CF_TRACE(str)                                                          \
  ::cuttlefish::ScopedTrace CF_SCOPED_TRACE_VAR_NAME(cf_internal_tracer_line_, \
                                                     __LINE__)(str)

#define CF_TRACEF(format, ...)                        \
  ::cuttlefish::ScopedTrace CF_SCOPED_TRACE_VAR_NAME( \
      cf_internal_tracer_line_,                       \
      __LINE__)(absl::StrFormat(format, ##__VA_ARGS__))

}  // namespace cuttlefish
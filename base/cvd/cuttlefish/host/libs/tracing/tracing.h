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

namespace cuttlefish {

void TraceEventBegin(const char* str);
void TraceEventBeginFormat(const char* format, ...);
void TraceEventEnd();

class ScopedTrace {
  public:
    template <typename... Args>
    inline ScopedTrace(const char* format, Args&&... args) {
      TraceEventBeginFormat(format, std::forward<Args>(args)...);
    }

    ~ScopedTrace() {
      TraceEventEnd();
    }
};

#define CF_SCOPED_TRACE_VAR_NAME2(x, y) x##y
#define CF_SCOPED_TRACE_VAR_NAME(x, y) CF_SCOPED_TRACE_VAR_NAME2(x, y)

#define CF_TRACE(fmt, ...) \
    ::cuttlefish::ScopedTrace CF_SCOPED_TRACE_VAR_NAME(___tracer, __LINE__)(fmt, ##__VA_ARGS__)

}  // namespace cuttlefish
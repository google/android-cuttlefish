/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <perfetto/tracing/tracing.h>
#include <perfetto/tracing/track_event.h>

#define CF_TRACE_CATEGORY "cuttlefish"

PERFETTO_DEFINE_CATEGORIES(perfetto::Category(CF_TRACE_CATEGORY)
                               .SetDescription("Cuttlefish Events")
                               .SetTags(CF_TRACE_CATEGORY));

#define CF_TRACE_EVENT(...) \
  TRACE_EVENT(CF_TRACE_CATEGORY, __VA_ARGS__)

#define CF_TRACE_EVENT_FUNC() \
  TRACE_EVENT(CF_TRACE_CATEGORY, __PRETTY_FUNCTION__)

namespace cuttlefish {

class ScopedTraceFlusher {
  public:
    ScopedTraceFlusher() = default;
    ~ScopedTraceFlusher();
};

// Performs any initialization required for tracing.
ScopedTraceFlusher InitializeTracing();

}  // cuttlefish
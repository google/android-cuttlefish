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

#include <atomic>
#include <cstdint>
#include <cstddef>

// TODO: see if Perfetto can add a header only library to their build.

enum PerfettoTeHlExtraType {
  PERFETTO_TE_HL_EXTRA_TYPE_REGISTERED_TRACK = 1,
  PERFETTO_TE_HL_EXTRA_TYPE_NAMED_TRACK = 2,
  PERFETTO_TE_HL_EXTRA_TYPE_TIMESTAMP = 3,
  PERFETTO_TE_HL_EXTRA_TYPE_DYNAMIC_CATEGORY = 4,
  PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_INT64 = 5,
  PERFETTO_TE_HL_EXTRA_TYPE_COUNTER_DOUBLE = 6,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_BOOL = 7,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_UINT64 = 8,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_INT64 = 9,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_DOUBLE = 10,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_STRING = 11,
  PERFETTO_TE_HL_EXTRA_TYPE_DEBUG_ARG_POINTER = 12,
  PERFETTO_TE_HL_EXTRA_TYPE_FLOW = 13,
  PERFETTO_TE_HL_EXTRA_TYPE_TERMINATING_FLOW = 14,
  PERFETTO_TE_HL_EXTRA_TYPE_FLUSH = 15,
  PERFETTO_TE_HL_EXTRA_TYPE_NO_INTERN = 16,
  PERFETTO_TE_HL_EXTRA_TYPE_PROTO_FIELDS = 17,
  PERFETTO_TE_HL_EXTRA_TYPE_PROTO_TRACK = 18,
  PERFETTO_TE_HL_EXTRA_TYPE_NESTED_TRACKS = 19,
};

enum PerfettoTeType {
  PERFETTO_TE_TYPE_SLICE_BEGIN = 1,
  PERFETTO_TE_TYPE_SLICE_END = 2,
  PERFETTO_TE_TYPE_INSTANT = 3,
  PERFETTO_TE_TYPE_COUNTER = 4,
};

struct PerfettoTeHlExtra {
  uint32_t type;
};

struct PerfettoTeHlExtraNamedTrack {
  struct PerfettoTeHlExtra header;
  const char* name;
  uint64_t id;
  uint64_t parent_uuid;
  bool is_name_static;
};

struct PerfettoTeTimestamp {
  // PerfettoTeTimestampType
  uint32_t clock_id;
  uint64_t value;
};

struct PerfettoTeHlExtraTimestamp {
  struct PerfettoTeHlExtra header;
  // The timestamp for this event.
  struct PerfettoTeTimestamp timestamp;
};

struct PerfettoTeHlExtraFlow {
  struct PerfettoTeHlExtra header;
  // Specifies that this event starts (or terminates) a flow (i.e. a link
  // between two events) identified by this id.
  uint64_t id;
};

// Perfetto does not have a header only bazel target.
struct PerfettoProducerBackendInitArgs;
struct PerfettoTeCategoryImpl;
struct PerfettoTeCategoryDescriptor {
  const char* name;
  const char* desc;
  const char** tags;
  size_t num_tags;
};

using PFN_PerfettoProducerBackendInitArgsCreate = struct PerfettoProducerBackendInitArgs* (*)(void);
using PFN_PerfettoProducerBackendInitArgsDestroy = void (*)(struct PerfettoProducerBackendInitArgs*);
using PFN_PerfettoProducerShutdown = void (*)(void);
using PFN_PerfettoProducerSystemInit = void (*)(const struct PerfettoProducerBackendInitArgs*);
using PFN_PerfettoTeCategoryImplCreate = struct PerfettoTeCategoryImpl* (*)(struct PerfettoTeCategoryDescriptor*);
using PFN_PerfettoTeCategoryImplDestroy = void (*)(struct PerfettoTeCategoryImpl*);
using PFN_PerfettoTeCategoryImplGetEnabled = std::atomic<bool>* (*)(struct PerfettoTeCategoryImpl*);
using PFN_PerfettoTeFlush = void (*)(void);
using PFN_PerfettoTeHlEmitImpl = void (*)(struct PerfettoTeCategoryImpl*, int32_t, const char*, struct PerfettoTeHlExtra* const*);
using PFN_PerfettoTeInit = void (*)(void);
using PFN_PerfettoTePublishCategories = void (*)(void);

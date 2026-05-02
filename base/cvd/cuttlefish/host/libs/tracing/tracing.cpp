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

#include "cuttlefish/host/libs/tracing/tracing.h"

#include "perfetto/tracing/core/trace_config.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace cuttlefish {

ScopedTraceFlusher::~ScopedTraceFlusher() {
  perfetto::TrackEvent::Flush();
}

ScopedTraceFlusher InitializeTracing() {
  []() {
    perfetto::TracingInitArgs args;
    args.backends |= perfetto::kSystemBackend;
    perfetto::Tracing::Initialize(args);

    perfetto::TrackEvent::Register();

    // Use a startup session to buffer trace events for a short
    // period so that trace events that occur after this function
    // ends but before the handshake with traced are still captured.
    perfetto::TraceConfig startup_trace_config;
    startup_trace_config.add_buffers()->set_size_kb(1024);
    auto* track_events_data_config = startup_trace_config.add_data_sources()->mutable_config();
    track_events_data_config->set_name("track_event");
    perfetto::protos::gen::TrackEventConfig track_event_config;
    track_event_config.add_enabled_categories("cuttlefish");
    track_events_data_config->set_track_event_config_raw(track_event_config.SerializeAsString());

    perfetto::Tracing::SetupStartupTracingOpts startup_opts;
    startup_opts.timeout_ms = 5000;
    startup_opts.backend = perfetto::kSystemBackend;
    perfetto::Tracing::SetupStartupTracingBlocking(std::move(startup_trace_config), startup_opts);
  }();
  return ScopedTraceFlusher();
}

}  // namespace cuttlefish

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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "cuttlefish/host/libs/tracing/tracing.pb.h"
#include "cuttlefish/host/libs/tracing/tracing_client.h"

namespace cuttlefish {

class TracingState {
 public:
  static TracingState& Get();

  ~TracingState() = default;

  TracingState(const TracingState&) = delete;
  TracingState& operator=(const TracingState&) = delete;

  void TraceBegin(std::string_view str) ABSL_LOCKS_EXCLUDED(mutex_);
  void TraceEnd() ABSL_LOCKS_EXCLUDED(mutex_);

  // If tracing is enabled, `mutex_` is held until the end of `AfterFork()`.
  void BeforeFork() ABSL_NO_THREAD_SAFETY_ANALYSIS;

  // If tracing is enabled, `mutex_` is already locked from `BeforeFork()`.
  void AfterFork(const bool is_child) ABSL_NO_THREAD_SAFETY_ANALYSIS;

  void AtExit() ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  struct ThreadTraceState {
    std::string thread_name;
    std::vector<TraceEventProto> trace_stack;
    // If present, the "Resuming After Fork" event. This event
    // is not replayed immediately in `AfterFork()` to avoid
    // spamming the trace with a bunch of processes that just
    // immediately `execv()`. Instead, it is replayed during the
    // next event to occur.
    std::optional<TraceEventProto> after_fork_event;
  };

  struct PreForkData {
    uint64_t parent_to_child_flow_id = 0;
    std::vector<TraceEventProto> forked_threads_trace_stack;
  };

  TracingState();

  // See note on ThreadTraceState::after_fork_event.
  void MaybeReplayAfterFork(ThreadTraceState& thread_state)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  const bool enabled_;

  absl::Mutex mutex_;
  std::unique_ptr<TracingClient> client_ ABSL_GUARDED_BY(mutex_);
  std::unordered_map<uint64_t, ThreadTraceState> thread_states_
      ABSL_GUARDED_BY(mutex_);
  std::optional<PreForkData> prefork_data_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace cuttlefish

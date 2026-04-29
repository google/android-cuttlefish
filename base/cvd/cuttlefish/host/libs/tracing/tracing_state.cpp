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

#include "cuttlefish/host/libs/tracing/tracing_state.h"

#include <time.h>

#include <mutex>

#include "absl/log/log.h"
#include "absl/random/random.h"

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/host/libs/tracing/tracing_client.h"
#include "cuttlefish/host/libs/tracing/tracing_utils.h"

namespace cuttlefish {
namespace {

bool TracingEnabled() {
  return StringFromEnv(kTracingSocketPathEnv).has_value();
}

uint64_t GetTimestampNs() {
  struct timespec ts;
  clock_gettime(CLOCK_BOOTTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

uint64_t GetNewFlowId() {
  thread_local absl::BitGen tlBitgen;
  return absl::Uniform<uint64_t>(tlBitgen);
}

void StateAtExit() { TracingState::Get().AtExit(); }
void StateBeforeFork() { TracingState::Get().BeforeFork(); }
void StateAfterForkParent() {
  TracingState::Get().AfterFork(/*is_child=*/false);
}
void StateAfterForkChild() { TracingState::Get().AfterFork(/*is_child=*/true); }

}  // namespace

TracingState& TracingState::Get() {
  static TracingState* sInstance = new TracingState();
  return *sInstance;
}

TracingState::TracingState() : enabled_(TracingEnabled()) {
  if (!enabled_) return;

  client_ = TracingClient::Create();
  if (!client_) return;

  int res = pthread_atfork(StateBeforeFork, StateAfterForkParent,
                           StateAfterForkChild);
  if (res != 0) {
    LOG(ERROR) << "Failed to register pthread fork callbacks.";
  }
  std::atexit(StateAtExit);
}

void TracingState::TraceBegin(std::string_view str) {
  if (!enabled_) return;

  const uint64_t timestamp = GetTimestampNs();
  const uint64_t tid = GetThreadId();

  absl::MutexLock lock(mutex_);
  if (!client_) return;

  ThreadTraceState& thread_state = thread_states_[tid];

  MaybeReplayAfterFork(thread_state);

  TraceEventProto new_event;
  new_event.set_type(TRACE_EVENT_TYPE_BEGIN);
  new_event.set_timestamp(timestamp);
  new_event.set_process_id(GetProcessId());
  new_event.set_process_name(GetProcessName());
  new_event.set_thread_id(tid);
  new_event.set_thread_name(thread_state.thread_name);
  new_event.set_name(str);

  client_->SendEventProto(new_event);
  thread_state.trace_stack.push_back(new_event);
}

void TracingState::TraceEnd() {
  if (!enabled_) return;

  const uint64_t timestamp = GetTimestampNs();
  const uint64_t tid = GetThreadId();

  absl::MutexLock lock(mutex_);
  if (!client_) return;

  auto it = thread_states_.find(tid);
  if (it == thread_states_.end()) {
    LOG(ERROR) << "TraceEnd called without thread trace state?";
    return;
  }
  ThreadTraceState& thread_state = it->second;

  MaybeReplayAfterFork(thread_state);

  if (thread_state.trace_stack.empty()) {
    LOG(ERROR) << "TraceEnd called with empty trace stack";
    return;
  }

  thread_state.trace_stack.pop_back();

  TraceEventProto proto;
  proto.set_type(TRACE_EVENT_TYPE_END);
  proto.set_timestamp(timestamp);
  proto.set_process_id(GetProcessId());
  proto.set_process_name(GetProcessName());
  proto.set_thread_id(tid);
  proto.set_thread_name(thread_state.thread_name);

  client_->SendEventProto(proto);
}

void TracingState::MaybeReplayAfterFork(ThreadTraceState& thread_state) {
  if (thread_state.after_fork_event) {
    for (const auto& event : thread_state.trace_stack) {
      client_->SendEventProto(event);
    }
    client_->SendEventProto(*thread_state.after_fork_event);
    thread_state.after_fork_event.reset();
  }
}

void TracingState::BeforeFork() {
  if (!enabled_) return;

  mutex_.lock();
  if (!client_) {
    mutex_.unlock();
    return;
  }

  const uint64_t parent_to_child_flow_id = GetNewFlowId();
  const uint64_t timestamp = GetTimestampNs();
  const uint64_t current_tid = GetThreadId();

  std::vector<TraceEventProto> forked_stack;

  auto it = thread_states_.find(current_tid);
  if (it != thread_states_.end()) {
    const auto& current_thread_state = it->second;
    TraceEventProto proto;
    proto.set_type(TRACE_EVENT_TYPE_INSTANT);
    proto.set_timestamp(timestamp);
    proto.set_process_id(GetProcessId());
    proto.set_process_name(GetProcessName());
    proto.set_thread_id(current_tid);
    proto.set_thread_name(current_thread_state.thread_name);
    proto.set_name("(Forking)");
    proto.set_flow(parent_to_child_flow_id);
    client_->SendEventProto(proto);

    forked_stack = current_thread_state.trace_stack;
  }

  prefork_data_ = PreForkData{
      .parent_to_child_flow_id = parent_to_child_flow_id,
      .forked_threads_trace_stack = std::move(forked_stack),
  };

  // Emit trace end events for all active traces in all threads
  for (auto& [thread_id, thread_state] : thread_states_) {
    for (auto it_stack = thread_state.trace_stack.rbegin();
         it_stack != thread_state.trace_stack.rend(); ++it_stack) {
      TraceEventProto proto;
      proto.set_type(TRACE_EVENT_TYPE_END);
      proto.set_timestamp(timestamp);
      proto.set_process_id(GetProcessId());
      proto.set_process_name(GetProcessName());
      proto.set_thread_id(thread_id);
      proto.set_thread_name(thread_state.thread_name);
      client_->SendEventProto(proto);
    }
  }

  // NOTE: mutex_ is still locked until the end of AfterFork().
}

void TracingState::AfterFork(const bool is_child) {
  if (!enabled_) return;

  // NOTE: mutex_ is still locked from BeforeFork().
  if (!client_) {
    mutex_.unlock();
    return;
  }

  const uint64_t timestamp = GetTimestampNs();
  const uint64_t thread_id = GetThreadId();

  if (is_child) {
    // In the child process, only the thread that called fork() survives.
    // Clean up trace events from all other threads.
    thread_states_.clear();

    auto& thread_state = thread_states_[thread_id];
    thread_state.thread_name = GetThreadName();
    thread_state.trace_stack = prefork_data_->forked_threads_trace_stack;
  }

  const std::string after_fork_event_str =
      is_child ? "(Child Resuming After Fork)" : "(Parent Resuming After Fork)";

  for (auto& [thread_id, thread_state] : thread_states_) {
    TraceEventProto after_fork_proto;
    after_fork_proto.set_type(TRACE_EVENT_TYPE_INSTANT);
    after_fork_proto.set_timestamp(timestamp);
    after_fork_proto.set_process_id(GetProcessId());
    after_fork_proto.set_process_name(GetProcessName());
    after_fork_proto.set_thread_id(thread_id);
    after_fork_proto.set_thread_name(thread_state.thread_name);
    after_fork_proto.set_name(after_fork_event_str);
    if (is_child) {
      after_fork_proto.set_flow(prefork_data_->parent_to_child_flow_id);
    }

    thread_state.after_fork_event = after_fork_proto;

    // Update the timestamp to the post fork timestamp so that the "restarted"
    // trace begin events show up as starting after the fork.
    for (TraceEventProto& trace_event : thread_state.trace_stack) {
      trace_event.set_timestamp(timestamp);
      trace_event.set_process_id(GetProcessId());
      trace_event.set_process_name(GetProcessName());
      trace_event.set_thread_id(thread_id);
      trace_event.set_thread_name(thread_state.thread_name);
    }
  }

  prefork_data_.reset();

  if (!is_child) {
    // This is deferred in child processes to avoid spamming the trace with
    // a mostly empty rows for processes that immedately `execvpe()`.
    MaybeReplayAfterFork(thread_states_[thread_id]);
  }

  mutex_.unlock();
}

void TracingState::AtExit() {
  if (!enabled_) return;

  absl::MutexLock lock(mutex_);

  if (!client_) {
    return;
  }

  const uint64_t timestamp = GetTimestampNs();
  for (auto& [thread_id, thread_state] : thread_states_) {
    for (auto it = thread_state.trace_stack.rbegin();
         it != thread_state.trace_stack.rend(); ++it) {
      TraceEventProto proto;
      proto.set_type(TRACE_EVENT_TYPE_END);
      proto.set_timestamp(timestamp);
      proto.set_process_id(GetProcessId());
      proto.set_process_name(GetProcessName());
      proto.set_thread_id(thread_id);
      proto.set_thread_name(thread_state.thread_name);
      client_->SendEventProto(proto);
    }
  }

  client_.reset();
}

}  // namespace cuttlefish

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

#include <chrono>
#include <thread>
#include <unordered_map>

#include <perfetto/tracing.h>
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/tracing/tracing.pb.h"

namespace cuttlefish {

class TracingServer {
 public:
  static Result<std::unique_ptr<TracingServer>> StartBlocking(
      absl::Duration timeout);

  ~TracingServer();

 private:
  explicit TracingServer(SharedFD socket);

  perfetto::Track GetTrackAndRegisterIfNeeded(const TraceEventProto& event)
      ABSL_LOCKS_EXCLUDED(mutex_);

  void WorkerThreadLoop();
  void WorkerProcessTraceEvent(const TraceEventProto& event);

  SharedFD socket_;
  std::thread thread_;

  absl::Mutex mutex_;
  bool shutting_down_ ABSL_GUARDED_BY(mutex_) = false;

  // Used to generate process uuids.
  absl::BitGen bitgen_ ABSL_GUARDED_BY(mutex_);

  struct RegisteredProcess {
    perfetto::Track process_track;
    std::unordered_map<uint64_t, perfetto::Track> registered_threads;
  };
  std::unordered_map<uint64_t, RegisteredProcess> registered_processes_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace cuttlefish
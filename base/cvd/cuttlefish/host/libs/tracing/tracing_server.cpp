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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * TracingServeried. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "cuttlefish/host/libs/tracing/tracing_server.h"

#include <unistd.h>
#include <cstdlib>

#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

#include <perfetto/tracing.h>
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/known_paths.h"
#include "cuttlefish/host/libs/tracing/tracing.pb.h"
#include "cuttlefish/host/libs/tracing/tracing_utils.h"

#define CUTTLEFISH_CATEGORY "cuttlefish"

PERFETTO_DEFINE_CATEGORIES(perfetto::Category(CUTTLEFISH_CATEGORY)
                               .SetDescription("Default events")
                               .SetTags("default"));

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace cuttlefish {

/*static*/
Result<std::unique_ptr<TracingServer>> TracingServer::StartBlocking(
    absl::Duration timeout) {
  auto socket_path_opt = StringFromEnv(kTracingSocketPathEnv);
  CF_EXPECT(socket_path_opt.has_value() && !socket_path_opt->empty(),
            kTracingSocketPathEnv << " was not set.");
  const std::string socket_path = *socket_path_opt;

  SharedFD socket =
      SharedFD::SocketLocalServer(socket_path, false, SOCK_DGRAM, 0600);
  CF_EXPECT(socket->IsOpen(), "Failed to create tracing server socket at "
                                  << socket_path << ": " << socket->StrError());

  perfetto::TracingInitArgs args;
  args.backends |= perfetto::kSystemBackend;
  perfetto::Tracing::Initialize(args);
  perfetto::TrackEvent::Register();

  // Perfetto's original expected usage was to have a long running service
  // that is initialized before a trace session starts.
  //
  // Cuttlefish wants to be able to trace the launch procedure where many
  // processes are connecting to an already running trace session.
  //
  // b/324031921 tracks adding a way to synchronize with traced during init
  // and to wait for a connection is traced is reachable. For now,
  // tracing will require explicit opt-in via a flag/envvar.
  absl::Time start = absl::Now();
  while ((absl::Now() - start) < timeout &&
         !TRACE_EVENT_CATEGORY_ENABLED(CUTTLEFISH_CATEGORY)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  CF_EXPECT(
      TRACE_EVENT_CATEGORY_ENABLED(CUTTLEFISH_CATEGORY),
      "Timed out after "
          << absl::FormatDuration(timeout)
          << " waiting for the Cuttlefish Perfetto category to be enabled."
          << " Please ensure that the Perfetto traced daemon is running and"
          << " that an active tracing session capturing the \"cuttlefish\""
          << " track event category has been started.");

  return std::unique_ptr<TracingServer>(new TracingServer(std::move(socket)));
}

TracingServer::TracingServer(SharedFD socket) : socket_(std::move(socket)) {
  thread_ = std::thread([this]() { WorkerThreadLoop(); });
}

TracingServer::~TracingServer() {
  {
    absl::MutexLock lock(mutex_);
    shutting_down_ = true;
  }

  socket_->Shutdown(SHUT_RDWR);

  if (thread_.joinable()) {
    thread_.join();
  }

  perfetto::TrackEvent::Flush();

  socket_->Close();
}

perfetto::Track TracingServer::GetTrackAndRegisterIfNeeded(
    const TraceEventProto& event) ABSL_LOCKS_EXCLUDED(mutex_) {
  absl::MutexLock lock(mutex_);

  const uint64_t process_id = event.process_id();
  const uint64_t thread_id = event.thread_id();

  auto process_it = registered_processes_.find(process_id);
  if (process_it == registered_processes_.end()) {
    const uint64_t process_uuid = absl::Uniform<uint64_t>(bitgen_);
    perfetto::Track process_track(process_uuid, perfetto::Track());
    auto desc = process_track.Serialize();
    auto* process_desc = desc.mutable_process();
    process_desc->set_pid(process_id);
    if (event.has_process_name() && !event.process_name().empty()) {
      process_desc->set_process_name(event.process_name());
    }
    perfetto::TrackEvent::SetTrackDescriptor(process_track, desc);

    bool unused = false;
    std::tie(process_it, unused) = registered_processes_.emplace(
        process_id, RegisteredProcess{
                        .process_track = process_track,
                    });
  }
  RegisteredProcess& registered_process = process_it->second;

  auto thread_it = registered_process.registered_threads.find(thread_id);
  if (thread_it == registered_process.registered_threads.end()) {
    const uint64_t thread_uuid = thread_id;
    perfetto::Track thread_track(thread_uuid, registered_process.process_track);
    auto desc = thread_track.Serialize();
    auto* thread_desc = desc.mutable_thread();
    thread_desc->set_pid(event.process_id());
    thread_desc->set_tid(thread_id);
    if (event.has_thread_name() && !event.thread_name().empty()) {
      thread_desc->set_thread_name(event.thread_name());
    }
    perfetto::TrackEvent::SetTrackDescriptor(thread_track, desc);

    bool unused = false;
    std::tie(thread_it, unused) =
        registered_process.registered_threads.emplace(thread_id, thread_track);
  }

  return thread_it->second;
}

void TracingServer::WorkerProcessTraceEvent(const TraceEventProto& event) {
  perfetto::Track thread_track = GetTrackAndRegisterIfNeeded(event);

  if (event.type() == TRACE_EVENT_TYPE_BEGIN) {
    if (event.has_flow()) {
      TRACE_EVENT_BEGIN(CUTTLEFISH_CATEGORY,
                        perfetto::DynamicString(event.name()), thread_track,
                        event.timestamp(),
                        perfetto::Flow::ProcessScoped(event.flow()));
    } else {
      TRACE_EVENT_BEGIN(CUTTLEFISH_CATEGORY,
                        perfetto::DynamicString(event.name()), thread_track,
                        event.timestamp());
    }
  } else if (event.type() == TRACE_EVENT_TYPE_END) {
    TRACE_EVENT_END(CUTTLEFISH_CATEGORY, thread_track, event.timestamp());
  } else if (event.type() == TRACE_EVENT_TYPE_INSTANT) {
    if (event.has_flow()) {
      TRACE_EVENT_INSTANT(CUTTLEFISH_CATEGORY,
                          perfetto::DynamicString(event.name()), thread_track,
                          event.timestamp(),
                          perfetto::Flow::Global(event.flow()));
    } else {
      TRACE_EVENT_INSTANT(CUTTLEFISH_CATEGORY,
                          perfetto::DynamicString(event.name()), thread_track,
                          event.timestamp());
    }
  }
}

void TracingServer::WorkerThreadLoop() {
  std::vector<char> buffer;
  buffer.resize(kMaxTracePacketSize);
  while (true) {
    {
      absl::MutexLock lock(mutex_);
      if (shutting_down_) {
        break;
      }
    }
    ssize_t bytes_read = socket_->Read(buffer.data(), buffer.size());
    if (bytes_read < 0) {
      bool shutting_down = false;
      {
        absl::MutexLock lock(mutex_);
        shutting_down = shutting_down_;
      }
      if (!shutting_down) {
        LOG(ERROR) << "Tracing server read error: " << socket_->StrError();
      }
      break;
    }
    if (bytes_read == 0) {
      break;
    }

    TraceEventProto proto;
    if (!proto.ParseFromArray(buffer.data(), bytes_read)) {
      LOG(ERROR) << "Tracing server failed to parse TraceEventProto";
      continue;
    }

    WorkerProcessTraceEvent(proto);
  }
}

}  // namespace cuttlefish
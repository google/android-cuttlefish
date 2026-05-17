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

#include "cuttlefish/host/libs/tracing/tracing.h"

#include <cstdlib>
#include <chrono>
#include <future>
#include <atomic>
#include <memory>
#include <optional>
#include <mutex>
#include <pthread.h>
#include <random>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "absl/log/log.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/tracing/perfetto.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

struct PerfettoFunctions {
  struct LibraryDeleter {
    void operator()(void* handle) const {
      if (handle != nullptr) {
        dlclose(handle);
      }
    }
  };
  std::unique_ptr<void, LibraryDeleter> handle;

  // Function pointers from C SDK using PFN_* types
  PFN_PerfettoProducerBackendInitArgsCreate PerfettoProducerBackendInitArgsCreate = nullptr;
  PFN_PerfettoProducerBackendInitArgsDestroy PerfettoProducerBackendInitArgsDestroy = nullptr;
  PFN_PerfettoProducerShutdown PerfettoProducerShutdown = nullptr;
  PFN_PerfettoProducerSystemInit PerfettoProducerSystemInit = nullptr;
  PFN_PerfettoTeCategoryImplCreate PerfettoTeCategoryImplCreate = nullptr;
  PFN_PerfettoTeCategoryImplDestroy PerfettoTeCategoryImplDestroy = nullptr;
  PFN_PerfettoTeCategoryImplGetEnabled PerfettoTeCategoryImplGetEnabled = nullptr;
  PFN_PerfettoTeFlush PerfettoTeFlush = nullptr;
  PFN_PerfettoTeHlEmitImpl PerfettoTeHlEmitImpl = nullptr;
  PFN_PerfettoTeInit PerfettoTeInit = nullptr;
  PFN_PerfettoTePublishCategories PerfettoTePublishCategories = nullptr;

  uint64_t* perfetto_te_process_track_uuid_ptr = nullptr;

  static Result<PerfettoFunctions> Load() {
    const std::string libperfetto_path = HostBinaryPath("libperfetto_c.so");

    void* handle = dlopen(libperfetto_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    CF_EXPECT(handle != nullptr, "Failed to load " << libperfetto_path);

    PerfettoFunctions funcs;
    funcs.handle.reset(handle);

    #define LOAD_SYMBOL(name) \
      funcs.name = reinterpret_cast<decltype(funcs.name)>(dlsym(funcs.handle.get(), #name)); \
      CF_EXPECT(funcs.name != nullptr, "Failed to resolve symbol " << #name << ": " << dlerror());

    LOAD_SYMBOL(PerfettoProducerBackendInitArgsCreate);
    LOAD_SYMBOL(PerfettoProducerBackendInitArgsDestroy);
    LOAD_SYMBOL(PerfettoProducerShutdown);
    LOAD_SYMBOL(PerfettoProducerSystemInit);
    LOAD_SYMBOL(PerfettoTeCategoryImplCreate);
    LOAD_SYMBOL(PerfettoTeCategoryImplDestroy);
    LOAD_SYMBOL(PerfettoTeCategoryImplGetEnabled);
    LOAD_SYMBOL(PerfettoTeFlush);
    LOAD_SYMBOL(PerfettoTeHlEmitImpl);
    LOAD_SYMBOL(PerfettoTeInit);
    LOAD_SYMBOL(PerfettoTePublishCategories);

    #undef LOAD_SYMBOL

    funcs.perfetto_te_process_track_uuid_ptr = reinterpret_cast<uint64_t*>(
        dlsym(funcs.handle.get(), "perfetto_te_process_track_uuid"));
    CF_EXPECT(funcs.perfetto_te_process_track_uuid_ptr != nullptr,
              "Failed to resolve symbol perfetto_te_process_track_uuid: " << dlerror());

    return funcs;
  }
};

uint64_t GetNewFlowId() {
    thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<uint64_t> distribution(0, std::numeric_limits<uint64_t>::max());
    return distribution(generator);
}

uint64_t GetThreadId() {
  return static_cast<uint64_t>(syscall(SYS_gettid));
}

uint64_t GetTimestamp() {
  struct timespec ts;
  clock_gettime(CLOCK_BOOTTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

struct TraceEvent {
  int type = 0;
  uint64_t timestamp = 0;
  uint64_t thread_id = 0;
  std::optional<std::string> str;
  std::optional<uint64_t> flow;
};

void TracingBeforeFork();
void TracingAfterForkChild();
void TracingAfterForkParent();
void TracingAtExit();

struct ThreadTraceState {
  uint64_t thread_id;
  std::string thread_name;
  std::vector<TraceEvent> trace_stack;
  std::optional<TraceEvent> after_fork_event;
};

thread_local std::shared_ptr<ThreadTraceState> tlThreadTraceState = nullptr;

// A dedicated helper thread for interacting with the Perfetto library.
//
// Perfetto makes use of static global data. The Perfetto library is
// loaded via dlopen() and unloaded via dlclose() so that the static
// global data can be reset.
//
// Perfetto also makes use of thread local data. When thread locals
// are used, the c runtime registers destructors for thread exit. One
// must ensure that all threads using these thread locals are destroyed
// before dlclose() from above. Otherwise, the thread local destructors
// might try to use destructor code that was already unloaded.
//
// With all of this, we restrict all interaction with the Perfetto library
// into a dedicated worker thread.
class PerfettoWorkerThread {
  public:
    PerfettoWorkerThread() {
      thread_ = std::thread(&PerfettoWorkerThread::WorkerThreadLoop, this);
    }

    ~PerfettoWorkerThread() {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        worker_shutting_down_ = true;
      }
      worker_condition_variable_.notify_one();

      thread_.join();
    }

    void EnqueueTraceEvent(TraceEvent event) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_events_.push_back(event);
      }

      worker_condition_variable_.notify_one();
    }

  private:
    bool LoadPerfetto() {
      auto result = PerfettoFunctions::Load();
      if (!result.ok()) {
        LOG(ERROR) << result.error();
        return false;
      }
      perfetto_ = std::move(*result);
      return true;
    }

    bool InitializeProducer() {
      struct PerfettoProducerBackendInitArgs* args =
        perfetto_.PerfettoProducerBackendInitArgsCreate();
      if (!args) {
        LOG(ERROR) << "Failed to create Perfetto backend init args.";
        return false;
      }
      perfetto_.PerfettoProducerSystemInit(args);
      perfetto_.PerfettoProducerBackendInitArgsDestroy(args);

      static struct PerfettoTeCategoryDescriptor desc = {
        .name = "cuttlefish",
        .desc = "Cuttlefish Events",
      };
      category_ = perfetto_.PerfettoTeCategoryImplCreate(&desc);
      if (!category_) {
        LOG(ERROR) << "Failed to create Perfetto category.";
        return false;
      }

      perfetto_.PerfettoTeInit();
      perfetto_.PerfettoTePublishCategories();

      static std::once_flag sInitializeOnceFlag;
      std::call_once(sInitializeOnceFlag, []() {
        int res = pthread_atfork(
            /*prepare*/[]() {
              TracingBeforeFork();
            },
            /*parent*/[]() {
              TracingAfterForkParent();
            },
            /*child*/[]() {
              TracingAfterForkChild();
            }
        );
        if (res != 0) {
          LOG(ERROR) << "Failed to register pthread fork callbacks.";
        }
        std::atexit([] {
          TracingAtExit();
        });
      });

      // Perfetto's original expected usage was to have a long running service
      // that is initialized before a trace session starts.
      //
      // Cuttlefish wants to be able to trace the launch procedure where many
      // processes are connecting to an already running trace session.
      //
      // b/324031921 tracks adding a way to synchronize with traced during init
      // and to wait for a connection is traced is reachable. For now, tracing
      // will require explicit opt-in via a flag/envvar.
      std::atomic<bool>* category_enabled =
        perfetto_.PerfettoTeCategoryImplGetEnabled(category_);
      while (!category_enabled->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      return true;
    }

    void ShutdownProducer() {
      perfetto_.PerfettoTeFlush();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      perfetto_.PerfettoTeCategoryImplDestroy(category_);
      perfetto_.PerfettoProducerShutdown();
    }

    void WorkerThreadLoop() {
      if (!LoadPerfetto()) {
        LOG(WARNING) << "Failed to initialize tracing: failed to load Perfetto.";
        return;
      }

      if (!InitializeProducer()) {
        LOG(WARNING) << "Failed to initialize tracing: failed to init Perfetto.";
        return;
      }

      while (true) {
        std::vector<TraceEvent> events_to_process;

        {
          std::unique_lock<std::mutex> lock(mutex_);
          worker_condition_variable_.wait(lock, [this] { return !trace_events_.empty() || worker_shutting_down_; });

          if (worker_shutting_down_ && trace_events_.empty()) {
            break;
          }

          events_to_process.swap(trace_events_);
        }

        for (auto& event : events_to_process) {
          WorkerProcessTraceEvent(event);
        }
      }

      ShutdownProducer();
    }

    uint64_t ProcessTrackUuid() const {
      return *perfetto_.perfetto_te_process_track_uuid_ptr;
    }

    void WorkerProcessTraceEvent(TraceEvent& event) {
      struct PerfettoTeHlExtraTimestamp timestamp_extra = {
        .header = {
          .type = PERFETTO_TE_HL_EXTRA_TYPE_TIMESTAMP,
        },
        .timestamp = {
          .clock_id = 6, // BOOTTIME
          .value = event.timestamp,
        },
      };

      const std::string thread_name = "Thread " + std::to_string(event.thread_id);
      struct PerfettoTeHlExtraNamedTrack track_extra = {
        .header = {
          .type = PERFETTO_TE_HL_EXTRA_TYPE_NAMED_TRACK,
        },
        .name = thread_name.c_str(),
        .id = event.thread_id,
        .parent_uuid = ProcessTrackUuid(),
        .is_name_static = false,
      };

      struct PerfettoTeHlExtraFlow flow_extra = {
        .header = {
          .type = PERFETTO_TE_HL_EXTRA_TYPE_FLOW,
        },
        .id = event.flow ? *event.flow : 0,
      };

      std::vector<struct PerfettoTeHlExtra*> extras;
      extras.push_back(reinterpret_cast<struct PerfettoTeHlExtra*>(&track_extra));
      extras.push_back(reinterpret_cast<struct PerfettoTeHlExtra*>(&timestamp_extra));
      if (event.flow) {
        extras.push_back(reinterpret_cast<struct PerfettoTeHlExtra*>(&flow_extra));
      }
      extras.push_back(nullptr);

      const char* event_str = event.str.has_value() ? event.str->c_str() : nullptr;
      perfetto_.PerfettoTeHlEmitImpl(category_, event.type, event_str, extras.data());
    }

    std::thread thread_;

    std::mutex mutex_;
    std::condition_variable worker_condition_variable_;
    bool worker_shutting_down_ = false;

    std::vector<TraceEvent> trace_events_;

    PerfettoFunctions perfetto_;
    struct PerfettoTeCategoryImpl* category_ = nullptr;
};

class PerfettoWrapper {
 public:
  static PerfettoWrapper& Get() {
    static PerfettoWrapper* sInstance = new PerfettoWrapper();
    return *sInstance;
  }

  ~PerfettoWrapper() = default;

  PerfettoWrapper(const PerfettoWrapper&) = delete;
  PerfettoWrapper& operator=(const PerfettoWrapper&) = delete;

  void Shutdown() {
    if (!enabled_) return;

    std::unique_lock<std::mutex> lock(mutex_);
    ShutdownLocked(/*is_forking=*/false);
  }

  void TraceBegin(const char* str) {
    if (!enabled_) return;

    TraceEvent trace_event = {
      .type = PERFETTO_TE_TYPE_SLICE_BEGIN,
      .timestamp = GetTimestamp(),
      .str = { str },
    };
    SendTraceEventToWorker(trace_event, /* modify_trace_stack=*/true);
  }

  void TraceEnd() {
    if (!enabled_) return;

    TraceEvent trace_event = {
      .type = PERFETTO_TE_TYPE_SLICE_END,
      .timestamp = GetTimestamp(),
    };
    SendTraceEventToWorker(trace_event, /* modify_trace_stack=*/true);
  }

  void BeforeFork() {
    if (!enabled_) return;

    const uint64_t parent_to_child_flow_id = GetNewFlowId();

    // NOTE: This is held until the end of AfterFork().
    mutex_.lock();

    prefork_data_ = PreForkData{
      .parent_to_child_flow_id = parent_to_child_flow_id,
    };

    if (tlThreadTraceState) {
      TraceEvent trace_event = {
        .type = PERFETTO_TE_TYPE_INSTANT,
        .timestamp = GetTimestamp(),
        .str = { "(Forking)" },
        .flow = { parent_to_child_flow_id },
      };
      SendTraceEventToWorkerLocked(trace_event, /* modify_trace_stack=*/false);

      prefork_data_->forked_threads_trace_stack = tlThreadTraceState->trace_stack;
    }

    ShutdownLocked(/*is_forking=*/true);
  }

  void AfterFork(const bool is_child) {
    if (!enabled_) return;

    // NOTE: mutex_.lock() still held from BeforeFork().

    const uint64_t timestamp = GetTimestamp();

    if (is_child) {
      // In the child process, only the thread that called fork() survives.
      // Clean up trace events from all other threads.
      thread_states_.clear();

      // If the parent process had active trace events, copy them into the
      // new child process. The thread local state is recreated as the
      // thread id will be changed after the fork.
      if (prefork_data_->forked_threads_trace_stack) {
        tlThreadTraceState = CreateThreadStateLocked();
        tlThreadTraceState->trace_stack = *prefork_data_->forked_threads_trace_stack;
      }
    }

    TraceEvent after_fork_event = {
      .type = PERFETTO_TE_TYPE_INSTANT,
      .timestamp = timestamp,
    };
    if (is_child) {
      after_fork_event.str = "(Child Resuming After Fork)";
      after_fork_event.flow = prefork_data_->parent_to_child_flow_id;
    } else {
      after_fork_event.str = "(Parent Resuming After Fork)";
    }

    for (auto& [_, thread_state] : thread_states_) {
      // After the fork, emit the "Resuming After Fork" trace event
      thread_state->after_fork_event = after_fork_event;
      thread_state->after_fork_event->thread_id = thread_state->thread_id;

      // Update the timestamp to the post fork timestamp so that the "restarted"
      // trace begin events show up as starting after the fork.
      for (auto& trace_event : thread_state->trace_stack) {
        trace_event.timestamp = timestamp;
      }
    }

    prefork_data_.reset();

    mutex_.unlock();
  }

  void AtExit() {
    if (!enabled_) return;

    Shutdown();
  }

 private:
  PerfettoWrapper() : enabled_(StringFromEnv("CVD_TRACE").has_value()) {}

  void ShutdownLocked(const bool is_forking) {
    // Emit trace end events for all of the active traces. If we are forking,
    // the trace begin events will be re-emitted after the fork.
    const uint64_t shutdown_timestamp = GetTimestamp();
    for (auto& [_, thread_state] : thread_states_) {
      for (auto it = thread_state->trace_stack.rbegin(); it != thread_state->trace_stack.rend(); ++it) {
        TraceEvent trace_event = {
          .type = PERFETTO_TE_TYPE_SLICE_END,
          .timestamp = shutdown_timestamp,
        };
        SendTraceEventToWorkerForThreadLocked(*thread_state, trace_event, /* modify_trace_stack=*/false);
      }
    }

    worker_.reset();
  }

  void EnsureWorkerInitializedLocked() {
    if (worker_.has_value()) {
      return;
    }
    worker_.emplace();
  }

  std::shared_ptr<ThreadTraceState> GetOrCreateThreadStateLocked() {
    if (tlThreadTraceState == nullptr) {
      tlThreadTraceState = CreateThreadStateLocked();
    }
    return tlThreadTraceState;
  }

  std::shared_ptr<ThreadTraceState> CreateThreadStateLocked() {
    auto state = std::make_shared<ThreadTraceState>();
    state->thread_id = GetThreadId();
    state->thread_name = "Thread " + std::to_string(state->thread_id);
    thread_states_[state->thread_id] = state;
    return state;
  }

  void RemoveThreadState(uint64_t thread_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    thread_states_.erase(thread_id);
  }

  void SendTraceEventToWorker(TraceEvent trace_event,
                              const bool modify_trace_stack = true) {
    std::lock_guard<std::mutex> lock(mutex_);
    SendTraceEventToWorkerLocked(trace_event, modify_trace_stack);
  }

  void SendTraceEventToWorkerLocked(TraceEvent trace_event,
                                    const bool modify_trace_stack = true) {
    EnsureWorkerInitializedLocked();

    auto thread_state = GetOrCreateThreadStateLocked();
    SendTraceEventToWorkerForThreadLocked(*thread_state, trace_event, modify_trace_stack);
 }

  void SendTraceEventToWorkerForThreadLocked(ThreadTraceState& thread_state,
                                             TraceEvent trace_event,
                                             const bool modify_trace_stack = true) {
    if (thread_state.after_fork_event) {
      for (const TraceEvent& event : thread_state.trace_stack) {
        worker_->EnqueueTraceEvent(event);
      }
      worker_->EnqueueTraceEvent(*thread_state.after_fork_event);

      thread_state.after_fork_event.reset();
    }

    trace_event.thread_id = thread_state.thread_id;

    worker_->EnqueueTraceEvent(trace_event);

    if (modify_trace_stack) {
      if (trace_event.type == PERFETTO_TE_TYPE_SLICE_BEGIN) {
        thread_state.trace_stack.push_back(trace_event);
      } else if (trace_event.type == PERFETTO_TE_TYPE_SLICE_END) {
        thread_state.trace_stack.pop_back();
      }
    }
  }

  const bool enabled_ = false;

  std::mutex mutex_;
  std::optional<PerfettoWorkerThread> worker_;
  std::unordered_map<uint64_t, std::shared_ptr<ThreadTraceState>> thread_states_;

  struct PreForkData {
    uint64_t parent_to_child_flow_id = 0;
    std::optional<std::vector<TraceEvent>> forked_threads_trace_stack;
  };
  std::optional<PreForkData> prefork_data_;
};

void TracingAtExit(){
  PerfettoWrapper::Get().Shutdown();
}

void TracingBeforeFork() {
  PerfettoWrapper::Get().BeforeFork();
}

void TracingAfterForkChild() {
  PerfettoWrapper::Get().AfterFork(true);
}

void TracingAfterForkParent() {
  PerfettoWrapper::Get().AfterFork(false);
}

}  // namespace

void TraceEventBegin(const char* str) {
  PerfettoWrapper::Get().TraceBegin(str);
}

void TraceEventEnd() {
  PerfettoWrapper::Get().TraceEnd();
}

void TraceEventBeginFormat(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  TraceEventBegin(buf);
}

} // namespace cuttlefish

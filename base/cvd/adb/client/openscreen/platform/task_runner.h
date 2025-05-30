/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <platform/api/task_runner.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

#include <android-base/thread_annotations.h>

namespace mdns {

// Openscreen API surface that allows for posting tasks. The underlying
// implementation may be single or multi-threaded, and all complication should
// be handled by the implementation class. The implementation must guarantee:
// (1) Tasks shall not overlap in time/CPU.
// (2) Tasks shall run sequentially, e.g. posting task A then B implies
//     that A shall run before B.
// (3) If task A is posted before task B, then any mutation in A happens-before
//     B runs (even if A and B run on different threads).
//
// Adb implementation: The PostPackagedTask* APIs are thread-safe.
// Another thread will handle dequeuing each item and calling it on the fdevent thread.
// Thus, the task runner thread is the fdevent thread, and IsRunningOnTaskRunner() shall always
// return true if calling from within the running Task.
class AdbOspTaskRunner final : public openscreen::TaskRunner {
  public:
    using Task = openscreen::TaskRunner::Task;

    // Must be called on the fdevent thread.
    explicit AdbOspTaskRunner();
    ~AdbOspTaskRunner() final;
    void PostPackagedTask(Task task) final;
    void PostPackagedTaskWithDelay(Task task, openscreen::Clock::duration delay) final;
    bool IsRunningOnTaskRunner() final;

    // The task executor thread.
    void TaskExecutorWorker();

  private:
    uint64_t thread_id_;
    std::mutex mutex_;
    std::multimap<std::chrono::time_point<std::chrono::steady_clock>, Task> tasks_
            GUARDED_BY(mutex_);
    std::atomic<bool> terminate_loop_ = false;
    std::condition_variable cv_;
    std::thread task_handler_;
};

}  // namespace mdns

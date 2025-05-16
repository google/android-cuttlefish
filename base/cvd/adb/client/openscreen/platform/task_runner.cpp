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

#include "client/openscreen/platform/task_runner.h"

#include <chrono>
#include <vector>

#include <android-base/logging.h>
#include <android-base/threads.h>
#include <platform/api/time.h>

#include "fdevent/fdevent.h"

using android::base::ScopedLockAssertion;
using namespace openscreen;

namespace mdns {

AdbOspTaskRunner::AdbOspTaskRunner() {
    fdevent_check_looper();
    thread_id_ = android::base::GetThreadId();
    task_handler_ = std::thread([this]() { TaskExecutorWorker(); });
}

AdbOspTaskRunner::~AdbOspTaskRunner() {
    if (task_handler_.joinable()) {
        terminate_loop_ = true;
        cv_.notify_one();
        task_handler_.join();
    }
}

void AdbOspTaskRunner::PostPackagedTask(Task task) {
    PostPackagedTaskWithDelay(std::move(task), openscreen::Clock::duration::zero());
}

void AdbOspTaskRunner::PostPackagedTaskWithDelay(Task task, Clock::duration delay) {
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.emplace(now + delay, std::move(task));
    }
    cv_.notify_one();
}

bool AdbOspTaskRunner::IsRunningOnTaskRunner() {
    return (thread_id_ == android::base::GetThreadId());
}

void AdbOspTaskRunner::TaskExecutorWorker() {
    for (;;) {
        {
            // Wait until there's a task available.
            std::unique_lock<std::mutex> lock(mutex_);
            ScopedLockAssertion assume_locked(mutex_);
            while (!terminate_loop_ && tasks_.empty()) {
                cv_.wait(lock);
            }
            if (terminate_loop_) {
                return;
            }

            // Wait until the task with the closest time point is ready to run.
            auto timepoint = tasks_.begin()->first;
            while (timepoint > std::chrono::steady_clock::now()) {
                cv_.wait_until(lock, timepoint);
                // It's possible that another task with an earlier time was added
                // while waiting for |timepoint|.
                timepoint = tasks_.begin()->first;

                if (terminate_loop_) {
                    return;
                }
            }
        }

        // Execute all tasks whose time points have passed.
        std::vector<Task> running_tasks;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            while (!tasks_.empty()) {
                auto task_with_delay = tasks_.begin();
                if (task_with_delay->first > std::chrono::steady_clock::now()) {
                    break;
                } else {
                    running_tasks.emplace_back(std::move(task_with_delay->second));
                    tasks_.erase(task_with_delay);
                }
            }
        }

        CHECK(!running_tasks.empty());
        std::packaged_task<int()> waitable_task([&] {
            for (Task& task : running_tasks) {
                task();
            }
            return 0;
        });

        fdevent_run_on_looper([&]() { waitable_task(); });

        waitable_task.get_future().wait();
    }
}
}  // namespace mdns

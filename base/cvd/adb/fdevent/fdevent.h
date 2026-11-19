/*
 * Copyright (C) 2006 The Android Open Source Project
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

#ifndef __FDEVENT_H
#define __FDEVENT_H

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <variant>

#include <android-base/thread_annotations.h>
#include <android-base/threads.h>

#include "adb_unique_fd.h"

// Events that may be observed
#define FDE_READ 0x0001
#define FDE_WRITE 0x0002
#define FDE_ERROR 0x0004
#define FDE_TIMEOUT 0x0008

struct fdevent;

typedef void (*fd_func)(int fd, unsigned events, void *userdata);
typedef void (*fd_func2)(struct fdevent* fde, unsigned events, void* userdata);

void invoke_fde(struct fdevent* fde, unsigned events);
std::string dump_fde(const fdevent* fde);

struct fdevent_event {
    fdevent* fde;
    unsigned events;
    fdevent_event(fdevent* pfde, unsigned ev) : fde(pfde), events(ev) {}
};

struct fdevent final {
    uint64_t id;

    unique_fd fd;

    uint16_t state = 0;
    std::optional<std::chrono::milliseconds> timeout;
    std::chrono::steady_clock::time_point last_active;

    std::variant<fd_func, fd_func2> func;
    void* arg = nullptr;
};

struct fdevent_context {
  public:
    virtual ~fdevent_context() = default;

    // Allocate and initialize a new fdevent object.
    fdevent* Create(unique_fd fd, std::variant<fd_func, fd_func2> func, void* arg);

    // Deallocate an fdevent object, returning the file descriptor that was owned by it.
    // Note that this calls Set, which is a virtual method, so destructors that call this must be
    // final.
    unique_fd Destroy(fdevent* fde);

  protected:
    virtual void Register(fdevent*) = 0;
    virtual void Unregister(fdevent*) = 0;

  public:
    // Change which events should cause notifications.
    virtual void Set(fdevent* fde, unsigned events) = 0;
    void Add(fdevent* fde, unsigned events);
    void Del(fdevent* fde, unsigned events);

    // Set a timeout on an fdevent.
    // If no events are triggered by the timeout, an FDE_TIMEOUT will be generated.
    // Note timeouts are not defused automatically; if a timeout is set on an fdevent, it will
    // trigger repeatedly every |timeout| ms.
    void SetTimeout(fdevent* fde, std::optional<std::chrono::milliseconds> timeout);

  protected:
    std::optional<std::chrono::milliseconds> CalculatePollDuration();
    void HandleEvents(const std::vector<fdevent_event>& events);

  private:
    // Run all pending functions enqueued via Run().
    void FlushRunQueue() EXCLUDES(run_queue_mutex_);

  public:
    // Loop until TerminateLoop is called, handling events.
    // Implementations should call FlushRunQueue on every iteration, and check the value of
    // terminate_loop_ to determine whether to stop.
    virtual void Loop() = 0;

    // Queue an operation to be run on the looper thread.
    void Run(std::function<void()> fn);

    // Test-only functionality:
    void TerminateLoop();
    virtual size_t InstalledCount() = 0;

    std::optional<uint64_t> looper_thread_id_ = std::nullopt;

  protected:
    // Interrupt the run loop.
    virtual void Interrupt() = 0;

    // Assert that the caller is executing in the context of the execution
    // thread that invoked Loop().
    void CheckLooperThread() const;

    std::atomic<bool> terminate_loop_ = false;

    std::map<int, fdevent> installed_fdevents_;

  private:
    uint64_t fdevent_id_ = 0;
    std::mutex run_queue_mutex_;
    std::deque<std::function<void()>> run_queue_ GUARDED_BY(run_queue_mutex_);

    std::set<fdevent*> fdevent_set_;
};

// Backwards compatibility shims that forward to the global fdevent_context.
fdevent* fdevent_create(int fd, fd_func func, void* arg);
fdevent* fdevent_create(int fd, fd_func2 func, void* arg);

unique_fd fdevent_release(fdevent* fde);
void fdevent_destroy(fdevent* fde);

void fdevent_set(fdevent *fde, unsigned events);
void fdevent_add(fdevent *fde, unsigned events);
void fdevent_del(fdevent *fde, unsigned events);
void fdevent_set_timeout(fdevent* fde, std::optional<std::chrono::milliseconds> timeout);
void fdevent_loop();

// Queue an operation to run on the looper event thread.
void fdevent_run_on_looper(std::function<void()> fn);

// The following functions are used only for tests.
void fdevent_terminate_loop();
size_t fdevent_installed_count();
void fdevent_reset();

fdevent_context* fdevent_get_ambient();

// We use macro so _FILE_ and _FUNCTION_ get the proper value if the check fails.

// Assert that the caller is executing in the context of the execution
// thread that invoked Loop().
#define CHECK_LOOPER_THREAD()                     \
    if (fdevent_get_ambient()->looper_thread_id_) \
    CHECK_EQ(*fdevent_get_ambient()->looper_thread_id_, android::base::GetThreadId())

// Assert that the caller is NOT executing in the context of the execution
// thread that invoked Loop().
#define CHECK_NOT_LOOPER_THREAD()                 \
    if (fdevent_get_ambient()->looper_thread_id_) \
    CHECK_NE(*fdevent_get_ambient()->looper_thread_id_, android::base::GetThreadId())

#endif

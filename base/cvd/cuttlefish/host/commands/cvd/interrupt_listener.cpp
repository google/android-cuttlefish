/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/interrupt_listener.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/signals.h"

namespace cuttlefish {
namespace {

static constexpr int CLOSED_FD = -1;
static constexpr int FD_IN_USE = -2;

// Write end of the pipe for the signal handler. May hold the following values:
// CLOSED_FD: Signals should not be sent through the pipe, if the thread that
// owns the fd encounters this value it must close the fd. FD_IN_USE: A signal
// was received and the handler is using the fd.
// >= 0: The write end of the signal pipe.
std::atomic<int> signal_socket_pair_write_end(CLOSED_FD);

// The stack is accessed from the main and the listener runner threads, so a
// mutex is needed.
std::mutex stack_mtx;
std::vector<InterruptListener> listener_stack;
std::thread listener_runner_thread;

/**
 * Loop executed by the background thread.
 */
void RunnerLoop(const int read_end) {
  for (;;) {
    int signal;
    auto bytes_read =
        TEMP_FAILURE_RETRY(recv(read_end, &signal, sizeof(signal), 0));
    if (bytes_read < 0) {
      auto err = errno;
      LOG(ERROR) << "Failed to receive signal from handler: " << strerror(err);
      // This is unrecoverable, so stop running (this is unlikely)
      break;
    }
    if (bytes_read == 0) {
      // The socket was closed
      break;
    }
    {
      std::lock_guard lock(stack_mtx);
      if (listener_stack.empty()) {
        // This could happen if the provided interrupt listener is disabled
        // after the signal is received but before this thread had a chance to
        // execute it. Under this circumstances the default handler for the
        // signal should have run, so deliver the signal again.
        kill(getpid(), signal);
        continue;
      }
      listener_stack.back()(signal);
    }
  }
  close(read_end);
}

void SignalHandler(int signal) {
  auto write_end = signal_socket_pair_write_end.exchange(FD_IN_USE);
  if (write_end < 0) {
    // This can only happen if the signal handler is disabled, re-send the
    // signal so it's handled by the appropriate handler.
    kill(getpid(), signal);
    return;
  }
  // Ignore result
  (void)send(write_end, &signal, sizeof(signal), 0);
  write_end = signal_socket_pair_write_end.exchange(write_end);
  if (write_end != FD_IN_USE) {
    // The signal handler was disabled while the handler was executing, need to
    // close the write_end.
    write_end = signal_socket_pair_write_end.exchange(CLOSED_FD);
    if (write_end >= 0) {
      close(write_end);
    }
  }
}

Result<void> StartHandling() {
  int fds[2];
  auto res = TEMP_FAILURE_RETRY(
      socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds));
  if (res != 0) {
    return CF_ERRNO("Failed to create socket pair for interrupt handler: ");
  }
  const int read_end = fds[0];
  const int write_end = fds[1];
  shutdown(read_end, SHUT_WR);

  // Make the write end available to the signal handler.
  signal_socket_pair_write_end.exchange(write_end);

  // Run the background thread with the read end of the socket.
  listener_runner_thread = std::thread([read_end]() { RunnerLoop(read_end); });

  ChangeSignalHandlers(SignalHandler, {SIGINT, SIGHUP, SIGTERM});

  return {};
}

void StopHandling() {
  ChangeSignalHandlers(SIG_DFL, {SIGINT, SIGHUP, SIGTERM});
  // Close the write end of the socket pair (or signal the handler to close it
  // itself if it's running
  int write_end = signal_socket_pair_write_end.exchange(CLOSED_FD);
  if (write_end > 0) {
    close(write_end);
  }
  // With the write end closed the thread will close its own end and return,
  // wait for that.
  listener_runner_thread.join();
}

void PopInterruptListener(size_t listener_index) {
  std::unique_lock lock(stack_mtx);
  CHECK(listener_stack.size() == listener_index + 1)
      << "Listeners disabled out of order: '" << listener_index
      << "' requested but stack size is '" << listener_stack.size() << "'";

  if (listener_stack.size() == 1) {
    // The stack is about to be empty, stop handling interrupts. The call to
    // StopHandling can't be made while holding the lock or we risk a deadlock
    // when joining the listener thread.
    lock.unlock();
    StopHandling();
    lock.lock();
  }

  listener_stack.pop_back();
}

}  // namespace

InterruptListenerHandle::~InterruptListenerHandle() { PopInterruptListener(listener_index_); }

Result<std::unique_ptr<InterruptListenerHandle>> PushInterruptListener(
    InterruptListener listener) {
  size_t listener_index;
  {
    std::lock_guard lock(stack_mtx);
    listener_index = listener_stack.size();
    listener_stack.push_back(listener);
  }
  if (!listener_runner_thread.joinable()) {
    CF_EXPECT(StartHandling());
  }
  return std::unique_ptr<InterruptListenerHandle>(
      new InterruptListenerHandle(listener_index));
}

}  // namespace cuttlefish

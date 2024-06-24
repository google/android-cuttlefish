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

#pragma once

#include <functional>
#include <memory>

#include "common/libs/utils/result.h"

namespace cuttlefish {

using InterruptListener = std::function<void(int)>;

class InterruptListenerHandle;
/**
 * Allows reacting to interrupt-like signals (SIGINT, SIGHUP, SIGTERM). A global
 * stack of interrupt listeners is maintained. When an interrupt signal is
 * received the listener at top of the stack is executed. Returns an object
 * that, when destroyed, disables the listener at the top of the stack,
 * re-enabling the previous one. This function is not thread-safe and shouldn't
 * be called from multiple threads, not even with synchronization. The listeners
 * are executed in a background thread, not in the actual interrupt handler.
 * It's ok to run blocking code in that thread, but disabling the listener may
 * wait for the thread to finish executing, so if the code in the listener could
 * wait for the thread that disables it a deadlock may occur. Similarly,
 * disabling the listener from inside the listener will lead to a deadlock. The
 * listener is given the actual signal received, one of SIGINT, SIGHUP or
 * SIGTERM.
 * To disable the listener (pop it from the listener stack) just destroy the
 * returned handle.
 */
Result<std::unique_ptr<InterruptListenerHandle>> PushInterruptListener(
    InterruptListener listener);

class InterruptListenerHandle {
 public:
  ~InterruptListenerHandle();

  InterruptListenerHandle(const InterruptListenerHandle&) = delete;
  InterruptListenerHandle& operator=(const InterruptListenerHandle&) = delete;

  InterruptListenerHandle(InterruptListenerHandle&& other) = delete;
  InterruptListenerHandle& operator=(InterruptListenerHandle&& other) = delete;

 private:
  friend Result<std::unique_ptr<InterruptListenerHandle>> PushInterruptListener(
      InterruptListener);
  InterruptListenerHandle(size_t listener_index)
      : listener_index_(listener_index) {}
  const size_t listener_index_;
};

}  // namespace cuttlefish

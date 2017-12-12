#pragma once

/*
 * Copyright (C) 2017 The Android Open Source Project
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

// Signaling mechanism that allows threads to signal changes to shared
// memory and to wait for signals.

namespace vsoc {
/**
 * Defines the strategy for signaling among threads on a single kernel.
 */
namespace SingleSidedSignal {
/**
 * Waits for a signal, assuming the the word at addr matches expected_state.
 * Will return immediately if the value does not match.
 * Callers must be equipped to cope with spurious returns.
 */
static void AwaitSignal(uint32_t expected_state, uint32_t* uaddr) {
  syscall(SYS_futex, uaddr, FUTEX_WAIT, expected_state, nullptr, nullptr, 0);
}

/**
 * Sends a signal to every thread in AwaitSignal() using the address in
 * uaddr.
 */
static void Signal(std::atomic<uint32_t>* uaddr) {
  syscall(SYS_futex, reinterpret_cast<int32_t*>(uaddr), FUTEX_WAKE, -1, nullptr,
          nullptr, 0);
}
}  // namespace SingleSidedSignal
}  // namespace vsoc

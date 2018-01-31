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

#include <stdlib.h>
#include <atomic>

#include "common/vsoc/shm/base.h"

namespace vsoc {

/**
 * Interface that defines signaling and waiting for signal.
 */
class RegionSignalingInterface {
 public:
  virtual ~RegionSignalingInterface(){};

  // Post a signal to the guest, the host, or both.
  // See futex(2) FUTEX_WAKE for details.
  //
  //   sides_to_signal: controls where the signal is sent
  //
  //   signal_addr: the memory location to signal. Must be within the region.
  virtual void SendSignal(layout::Sides sides_to_signal,
                          std::atomic<uint32_t>* signal_addr) = 0;

  // This implements the following:
  // if (*signal_addr == last_observed_value)
  //   wait_for_signal_at(signal_addr);
  //
  // Note: the caller still needs to check the value at signal_addr because
  // this function may return early for reasons that are implementation-defined.
  // See futex(2) FUTEX_WAIT for details.
  //
  //   signal_addr: the memory that will be signaled. Must be within the region.
  //
  //   last_observed_value: the value that motivated the calling code to wait.
  virtual void WaitForSignal(std::atomic<uint32_t>* signal_addr,
                             uint32_t last_observed_value) = 0;
};

}  // namespace vsoc

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

// Object that represents a region on the Host

#include <stdlib.h>
#include <sys/mman.h>
#include <atomic>
#include <cstdint>

#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/glog/logging.h"
#include "common/vsoc/shm/base.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {

class RegionBase;

class RegionWorker {
 public:
  explicit RegionWorker(RegionBase* region);
  ~RegionWorker();
  void Work();

 protected:
  RegionBase* region_;
  std::thread thread_;
  volatile bool stopping_{};
};

/**
 * Base class to access a region in VSoC shared memory.
 * This class holds the methods that do not depend on the type to simplify
 * the template expansions.
 *
 * This should not be directly instantiated. While technically concrete
 * a resonable implementation needs additional methods:
 *
 *   * Knowledge of the region's layout (see RegionSingleton)
 *   * Guest or host-specific code to gain access to the region
 */
class RegionBase {
 public:
  virtual ~RegionBase();

  // Returns the size of the entire region, including the signal tables.
  uint32_t region_size() const {
    return region_desc_.region_end_offset - region_desc_.region_begin_offset;
  }

  // Returns the size of the region that is usable for region-specific data.
  uint32_t region_data_size() const {
    return region_size() - region_desc_.offset_of_region_data;
  }

  // Returns a pointer to the table that will be scanned for signals
  virtual vsoc_signal_table_layout* incoming_signal_table() = 0;

  // Returns a pointer to the table that will be used to post signals
  virtual vsoc_signal_table_layout* outgoing_signal_table() = 0;

  // Interrupt our peer, causing it to scan the outgoing_signal_table
  virtual void InterruptPeer() = 0;

  // Wake the local signal table scanner. Primarily used during shutdown
  virtual void InterruptSelf() = 0;

  // Wait for an interrupt from our peer
  virtual void WaitForInterrupt() = 0;

  // Scan in the incoming signal table, issuing futex calls for any posted
  // signals and then reposting them to the peer if they were round-trip
  // signals.
  //
  //   stopping: Set to true when it is time for the thread to exit.
  //             This must be volatile because stopping will be changed
  //             while the thread is running.
  void ProcessSignalsFromPeer(volatile bool* stopping);

  // Post a signal to the guest, the host, or both.
  // See futex(2) FUTEX_WAKE for details.
  //
  //   sides_to_signal: controls where the signal is sent
  //
  //   signal_addr: the memory location to signal. Must be within the region.
  void SendSignal(layout::Sides sides_to_signal, uint32_t* signal_addr);

  // Post a signal to our peer for a specific memeory location.
  // See futex(2) FUTEX_WAKE for details.
  //
  //   signal_addr: the memory location to signal. Must be within the region.
  //
  //   round_trip: true if there may be waiters on both sides of the shared
  //               memory.
  void SendSignalToPeer(uint32_t* signal_addr, bool round_trip);

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
  void WaitForSignal(uint32_t* signal_addr, uint32_t last_observed_value);

  // Starts the signal table scanner. This must be invoked by subclasses, which
  // must store the returned unique_ptr as a class member.
  std::unique_ptr<RegionWorker> StartWorker();

 protected:
  RegionBase() {}

  template <typename T>
  T* region_offset_to_pointer(uint32_t offset) {
    if (offset > region_size()) {
      LOG(FATAL) << __FUNCTION__ << ": " << offset << " not in region @"
                 << region_base_;
    }
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(region_base_) +
                                offset);
  }

  template <typename T>
  uint32_t pointer_to_region_offset(T* ptr) {
    uint32_t rval = reinterpret_cast<uintptr_t>(ptr) -
                    reinterpret_cast<uintptr_t>(region_base_);
    if (rval > region_size()) {
      LOG(FATAL) << __FUNCTION__ << ": " << ptr << " not in region @"
                 << region_base_;
    }
    return rval;
  }

  vsoc_device_region region_desc_{};
  void* region_base_{};
};

}  // namespace vsoc

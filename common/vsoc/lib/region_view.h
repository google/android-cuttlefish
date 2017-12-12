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

#include <functional>
#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/glog/logging.h"
#include "common/vsoc/lib/region_signaling_interface.h"
#include "common/vsoc/lib/region_control.h"
#include "common/vsoc/shm/base.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {

class RegionView;

/**
 * Represents a task that is tied to a RegionView.
 *
 * This is currently used for the task that forwards futexes across the
 * shared memory window.
 */
class RegionWorker {
 public:
  explicit RegionWorker(RegionView* region);
  ~RegionWorker();
  void Work();

 protected:
  RegionView* region_;
  std::thread thread_;
  volatile bool stopping_{};
};

/**
 * Base class to access a mapped region in VSoC shared memory.
 * This class holds the methods that depends on the region's memory having an
 * address. The RegionControl class holds the methods that can be invoked
 * without mapping the region.
 */
class RegionView : public RegionSignalingInterface {
 public:
  virtual ~RegionView();

  bool Open(const char* region_name, const char* domain = nullptr);

  // Returns a pointer to the table that will be scanned for signals
  const vsoc_signal_table_layout& incoming_signal_table();

  // Returns a pointer to the table that will be used to post signals
  const vsoc_signal_table_layout& outgoing_signal_table();

  // Returns true iff an interrupt is queued in the signal table
  bool HasIncomingInterrupt() {
    return *region_offset_to_pointer<std::atomic<uint32_t>>(
        incoming_signal_table().interrupt_signalled_offset);
  }

  // Wake any threads waiting for an interrupt. This is generally used during
  // shutdown.
  void InterruptSelf() { control_->InterruptSelf(); }

  // Interrupt our peer if an interrupt is not already on the way.
  // Returns true if the interrupt was sent, false if an interrupt was already
  // pending.
  bool MaybeInterruptPeer();

  // Scan in the incoming signal table, issuing futex calls for any posted
  // signals and then reposting them to the peer if they were round-trip
  // signals.
  //
  //   signal_handler: An action to perform on every offset signalled by our
  //   peer, usually a FUTEX_WAKE call, but can be customized for other
  //   purposes.
  void ProcessSignalsFromPeer(
      std::function<void(uint32_t*)> signal_handler);

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

  // Waits until an interrupt appears on this region, then clears the
  // interrupted flag and returns.
  void WaitForInterrupt();

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

  // Returns a pointer to the start of region data that is cast to the given
  // type.  Initializers that run in the launcher use this to get a typed view of the region. Most other cases should be handled via TypedRegionView.
  template <typename LayoutType>
  LayoutType* GetLayoutPointer() {
    return this->region_offset_to_pointer<LayoutType>(
        control_->region_desc().offset_of_region_data);
  }

 protected:
  template <typename T>
  T* region_offset_to_pointer(vsoc_reg_off_t offset) {
    if (offset > control_->region_size()) {
      LOG(FATAL) << __FUNCTION__ << ": " << offset << " not in region @"
                 << region_base_;
    }
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(region_base_) +
                                offset);
  }

  template <typename T>
  const T& region_offset_to_reference(vsoc_reg_off_t offset) const {
    if (offset > control_->region_size()) {
      LOG(FATAL) << __FUNCTION__ << ": " << offset << " not in region @"
                 << region_base_;
    }
    return *reinterpret_cast<const T*>(
        reinterpret_cast<uintptr_t>(region_base_) + offset);
  }

  // Calculates an offset based on a pointer in the region. Crashes if the
  // pointer isn't in the region.
  // This is mostly for the RegionView's internal plumbing. Use TypedRegionView
  // and RegionLayout to avoid this in most cases.
  template <typename T>
  vsoc_reg_off_t pointer_to_region_offset(T* ptr) {
    vsoc_reg_off_t rval = reinterpret_cast<uintptr_t>(ptr) -
                          reinterpret_cast<uintptr_t>(region_base_);
    if (rval > control_->region_size()) {
      LOG(FATAL) << __FUNCTION__ << ": " << ptr << " not in region @"
                 << region_base_;
    }
    return rval;
  }

  std::shared_ptr<RegionControl> control_;
  void* region_base_{};
};

}  // namespace vsoc

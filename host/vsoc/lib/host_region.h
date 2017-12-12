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

#include "common/vsoc/lib/region.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <atomic>
#include <cstdint>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "uapi/vsoc_shm.h"

namespace vsoc {

/**
 * Accessor class for VSoC regions designed for use from processes on the
 * host. This mainly affects the implementatio of Open.
 *
 * Subclass to use this or use TypedRegionView with a suitable Layout.
 */
class OpenableRegionView : public RegionView {
 public:
  virtual ~OpenableRegionView() {}

  // Returns a pointer to the table that will be scanned for signals
  virtual vsoc_signal_table_layout* incoming_signal_table() {
    return &region_desc_.guest_to_host_signal_table;
  }

  // Returns a pointer to the table that will be used to post signals
  virtual vsoc_signal_table_layout* outgoing_signal_table() {
    return &region_desc_.host_to_guest_signal_table;
  }

  // Interrupt our peer, causing it to scan the outgoing_signal_table
  virtual void InterruptPeer() {
    if (!region_offset_to_pointer<std::atomic<uint32_t>>(
            outgoing_signal_table()->interrupt_signalled_offset)->exchange(1)) {
      uint64_t one = 1;
      ssize_t rval = outgoing_interrupt_fd_->Write(&one, sizeof(one));
      if (rval != sizeof(one)) {
        LOG(FATAL) << __FUNCTION__ << ": rval (" << rval << ") != sizeof(one))";
      }
    }
  }

  // Wake the local signal table scanner. Primarily used during shutdown
  virtual void InterruptSelf() {
    uint64_t one = 1;
    ssize_t rval = incoming_interrupt_fd_->Write(&one, sizeof(one));
    if (rval != sizeof(one)) {
      LOG(FATAL) << __FUNCTION__ << ": rval (" << rval << ") != sizeof(one))";
    }
  }

  // Wait for an interrupt from our peer
  virtual void WaitForInterrupt() {
    while (1) {
      if (region_offset_to_pointer<std::atomic<uint32_t>>(
              incoming_signal_table()->interrupt_signalled_offset)->exchange(0)) {
        // The eventfd isn't cleared by design. This is a optimization: if
        // an interrupt is pending we avoid the sleep, lowering the latency.
        // It does mean that we do some extra work the next time that we go
        // to sleep. However, an extra delay in sleeping is preferable to a
        // delay in waking.
        return;
      }
      // Check then act isn't a problem here: the other side does
      // the following things in exactly this order:
      //   1. exchanges 1 with interrupt_signalled
      //   2. if interrupt_signalled was 0 it increments the eventfd
      // eventfd increments are persistent, so if interrupt_signalled was set
      // back to 1 while we are going to sleep the sleep will return
      // immediately.
      uint64_t missed{};
      avd::SharedFDSet readset;
      readset.Set(incoming_interrupt_fd_);
      avd::Select(&readset, NULL, NULL, NULL);
      ssize_t rval = incoming_interrupt_fd_->Read(&missed, sizeof(missed));
      if (rval != sizeof(missed)) {
        LOG(FATAL) << __FUNCTION__ << ": rval (" << rval <<
            ") != sizeof(missed))";
      }
      if (!missed) {
        LOG(FATAL) << __FUNCTION__ << ": woke with 0 interrupts";
      }
    }
  }

 protected:
  OpenableRegionView() {}

  bool Open(const char* name, const char* domain = nullptr);

  avd::SharedFD incoming_interrupt_fd_;
  avd::SharedFD outgoing_interrupt_fd_;
};

/**
 * This class adds methods that depend on the Region's type.
 * This may be directly constructed. However, it may be more effective to
 * subclass it, adding region-specific methods.
 *
 * Layout should be VSoC shared memory compatible, defined in common/vsoc/shm,
 * and should have a constant string region name.
 */
template <typename Layout>
class TypedRegionView : public OpenableRegionView {
 public:
  /* Returns a pointer to the region with a type that matches the layout */
  Layout* data() {
    return reinterpret_cast<Layout*>(reinterpret_cast<uintptr_t>(region_base_) +
                                     region_desc_.offset_of_region_data);
  }

  TypedRegionView() {}

  bool Open(const char* domain = nullptr) {
    return OpenableRegionView::Open(Layout::region_name, domain);
  }
};

}  // namespace vsoc

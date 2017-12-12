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
#include "uapi/vsoc_shm.h"

namespace vsoc {

/**
 * Accessor class for VSoC regions designed for use from processes on the
 * host. This mainly affects the implementation of Open.
 *
 * Subclass to use this or use TypedRegionView with a suitable Layout.
 */
class OpenableRegionView : public RegionView {
 public:
  virtual ~OpenableRegionView() {}

  // Returns a pointer to the table that will be scanned for signals
  virtual vsoc_signal_table_layout* incoming_signal_table() override {
    return &region_desc_.host_to_guest_signal_table;
  }

  // Returns a pointer to the table that will be used to post signals
  virtual vsoc_signal_table_layout* outgoing_signal_table() override {
    return &region_desc_.guest_to_host_signal_table;
  }

  virtual void InterruptPeer() override;
  virtual void InterruptSelf() override;
  virtual void WaitForInterrupt() override;

 protected:
  OpenableRegionView() {}
  bool Open(const char* region_name, const char* domain);
  int CreateFdScopedPermission(const char* managed_region_name,
                               uint32_t* owner_ptr, uint32_t owned_val,
                               vsoc_reg_off_t begin_offset,
                               vsoc_reg_off_t end_offset);
  avd::SharedFD region_fd_;
};

}  // namespace vsoc

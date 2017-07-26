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

enum {
  // Means an unrecoverable error ocurred, aborting is usually the best handling
  // strategy in this case.
  VSOC_PERM_ERROR = -2,
  // Means that the permission could not be created because someone else
  // reserved the memory first. Find another area of memory and try again.
  VSOC_PERM_OWNED = -1,
};

/**
 * Accessor class for VSoC regions designed for use from processes on the
 * host. This mainly affects the implementation of Open.
 *
 * Subclass to use this or use TypedRegion with a suitable Layout.
 */
class OpenableRegion : public RegionBase {
 public:
  virtual ~OpenableRegion() {}

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
  OpenableRegion() {}
  bool Open(const char* region_name);
  int CreateFdScopedPermission(const char* managed_region_name,
                               uint32_t* owner_ptr,
                               uint32_t owned_val,
                               vsoc_reg_off_t begin_offset,
                               vsoc_reg_off_t end_offset);
  avd::SharedFD region_fd_;
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
class TypedRegion : public OpenableRegion {
 public:
  /* Returns a pointer to the region with a type that matches the layout */
  Layout* data() {
    return reinterpret_cast<Layout*>(reinterpret_cast<uintptr_t>(region_base_) +
                                     region_desc_.offset_of_region_data);
  }
  TypedRegion() {}

  bool Open() { return OpenableRegion::Open(Layout::region_name); }
};

/**
 * Adds methods to create file descriptor scoped permissions. Just like
 * TypedRegion it can be directly constructed or subclassed.
 *
 * The Layout type must (in addition to requirements for TypedRegion) also
 * provide a nested type for the layout of the managed region.
 */
template <typename Layout>
class ManagerRegion : public TypedRegion<Layout> {
 public:
  ManagerRegion() = default;
  /**
   * Creates a fd scoped permission on the managed region.
   *
   * The managed_region_fd is in/out parameter that can be a not yet open file
   * descriptor. If the fd is not open yet it will open the managed region
   * device and then create the permission. If the function returns EBUSY
   * (meaning that we lost the race to acquire the memory) the same fd can (and
   * is expected to) be used in a subsequent call to create a permission on
   * another memory location.
   *
   * On success returns an open fd with the requested permission asociated to
   * it. If another thread/process acquired ownership of *owner_ptr before this
   * one returns VSOC_PERM_OWNED. Returns VSOC_PERM_ERROR otherwise.
   */
  int CreateFdScopedPermission(uint32_t* owner_ptr,
                               uint32_t owned_val,
                               vsoc_reg_off_t begin_offset,
                               vsoc_reg_off_t end_offset) {
    return OpenableRegion::CreateFdScopedPermission(
        Layout::ManagedRegion::region_name,
        owner_ptr,
        owned_val,
        begin_offset,
        end_offset);
  }
};

}  // namespace vsoc

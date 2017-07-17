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
 * host. This mainly affects the implementatio of Open.
 *
 * Subclass to use this or use TypedRegion with a suitable Layout.
 */
class OpenableRegion : public RegionBase {
 public:
  virtual ~OpenableRegion() {}

 protected:
  OpenableRegion() {}

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
class TypedRegion : public OpenableRegion {
 public:
  /* Returns a pointer to the region with a type that matches the layout */
  Layout* data() {
    return reinterpret_cast<Layout*>(reinterpret_cast<uintptr_t>(region_base_) +
                                     region_desc_.offset_of_region_data);
  }

  TypedRegion() {}

  bool Open(const char* domain = nullptr) {
    return OpenableRegion::Open(Layout::region_name, domain);
  }
};

}  // namespace vsoc

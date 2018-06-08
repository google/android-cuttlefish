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

#include "common/vsoc/lib/typed_region_view.h"

namespace vsoc {

/**
 * Adds methods to create file descriptor scoped permissions. Just like
 * TypedRegionView it can be directly constructed or subclassed.
 *
 * The Layout type must (in addition to requirements for TypedRegionView) also
 * provide a nested type for the layout of the managed region.
 */
template <typename View, typename Layout>
class ManagerRegionView : public TypedRegionView<View, Layout> {
 public:
  ManagerRegionView() = default;
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
   * one returns -EBUSY, returns a different negative number otherwise.
   */
  int CreateFdScopedPermission(uint32_t* owner_ptr, uint32_t owned_val,
                               uint32_t begin_offset,
                               uint32_t end_offset) {
    return this->control_->CreateFdScopedPermission(
        Layout::ManagedRegion::region_name,
        this->pointer_to_region_offset(owner_ptr), owned_val, begin_offset,
        end_offset);
  }
};

}  // namespace vsoc

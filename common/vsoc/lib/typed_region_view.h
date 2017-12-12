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

#include "common/vsoc/lib/region_view.h"

#include <map>
#include <mutex>
#include <string>

namespace vsoc {

/**
 * This class adds methods that depend on the Region's type.
 * This may be directly constructed. However, it may be more effective to
 * subclass it, adding region-specific methods.
 *
 * Layout should be VSoC shared memory compatible, defined in common/vsoc/shm,
 * and should have a constant string region name.
 */
template <typename LayoutType>
class TypedRegionView : public RegionView {
 public:
  using Layout = LayoutType;

  /* Returns a pointer to the region with a type that matches the layout */
  LayoutType* data() {
    return this->region_offset_to_pointer<LayoutType>(
        control_->region_desc().offset_of_region_data);
  }

  const LayoutType& data() const {
    return this->region_offset_to_reference<LayoutType>(
        control_->region_desc().offset_of_region_data);
  }

  bool Open(const char* domain = nullptr) {
    return RegionView::Open(LayoutType::region_name, domain);
  }
};

}  // namespace vsoc

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
#include "common/vsoc/shm/gralloc_layout.h"

#include <string>

namespace vsoc {
namespace gralloc {

// Allows access to the gralloc buffer region from host side. It needs to be a
// different class than the one on guest side because of the required
// interactions with the kernel on the guest.
// Initially this class only returns a pointer to a buffer in memory given a
// region offset, which is enough for now since it's only used by the hwcomposer
// (which gets all other information from the guest side hwcomposer) and by the
// VNC server (which uses only the frame buffer and gets the information it
// needs from the framebuffer region).
class GrallocBufferRegionView
    : vsoc::TypedRegionView<
        GrallocBufferRegionView,
        vsoc::layout::gralloc::GrallocBufferLayout> {
   public:
  GrallocBufferRegionView() = default;
  GrallocBufferRegionView(const GrallocBufferRegionView&) = delete;
  GrallocBufferRegionView& operator=(const GrallocBufferRegionView&) = delete;

  uint8_t* OffsetToBufferPtr(uint32_t offset);
};

}  // namespace gralloc
}  // namespace vsoc

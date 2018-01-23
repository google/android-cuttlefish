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

#include <memory>

#include "common/vsoc/lib/typed_region_view.h"
#include "common/vsoc/shm/framebuffer_layout.h"

namespace vsoc {
namespace framebuffer {

/* Grants access to the framebuffer region. It only knows about the available
 * buffer space, but not about the distribution of that space, it's up to the
 * gralloc hal to break it into different buffers.
 * This region is temporary since the framebuffer should be integrated into the
 * gralloc buffers region.*/
class FrameBufferRegionView
    : public vsoc::TypedRegionView<
          vsoc::layout::framebuffer::FrameBufferLayout> {
 public:

#if defined(CUTTLEFISH_HOST)
  static std::shared_ptr<FrameBufferRegionView> GetInstance(
      const char* domain) {
    return RegionView::GetInstanceImpl<FrameBufferRegionView>(
        [](std::shared_ptr<FrameBufferRegionView> region, const char* domain) {
          return region->Open(domain);
        },
        domain);
  }
#else
  static std::shared_ptr<FrameBufferRegionView> GetInstance() {
    return RegionView::GetInstanceImpl<FrameBufferRegionView>(
        std::mem_fn(&FrameBufferRegionView::Open));
  }
#endif

  size_t total_buffer_size() const;
  uint32_t first_buffer_offset() const;
  // Gets a pointer to an offset of the region.
  void* GetBufferFromOffset(uint32_t offset);
};

}  // namespace framebuffer
}  // namespace vsoc

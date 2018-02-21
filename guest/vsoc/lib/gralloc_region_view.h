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

#include <common/vsoc/shm/gralloc_layout.h>
#include <guest/vsoc/lib/manager_region_view.h>
#include <memory>
#include <stdlib.h>

namespace vsoc {
namespace gralloc {

class GrallocRegionView : public vsoc::ManagerRegionView<
                          vsoc::layout::gralloc::GrallocManagerLayout> {
 public:
  GrallocRegionView() = default;
  // Allocates a gralloc buffer of (at least) the specified size. Returns a file
  // descriptor that exposes the buffer when mmapped from 0 to (the page
  // aligned) size (and fails to mmap anything outside of that range) or a
  // negative number in case of error (e.g not enough free memory left).
  // TODO(jemoreira): Include debug info like stride, width, height, etc
  int AllocateBuffer(size_t size, uint32_t* begin_offset = nullptr);

  static std::shared_ptr<GrallocRegionView> GetInstance();

 protected:
  GrallocRegionView(const GrallocRegionView&) = delete;
  GrallocRegionView & operator=(const GrallocRegionView&) = delete;

  bool Open();

  uint32_t offset_of_buffer_memory_{};
  uint32_t total_buffer_memory_{};
};

} // namespace gralloc
} // namespace vsoc

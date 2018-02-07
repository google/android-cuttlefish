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

#include "common/vsoc/lib/framebuffer_region_view.h"

using vsoc::framebuffer::FrameBufferRegionView;

size_t FrameBufferRegionView::total_buffer_size() const {
  return static_cast<size_t>(control_->region_data_size());
}

uint32_t FrameBufferRegionView::first_buffer_offset() const {
  return control_->region_size() - control_->region_data_size();
}

void* FrameBufferRegionView::GetBufferFromOffset(uint32_t offset) {
  return region_offset_to_pointer<void>(offset);
}

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

#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/lock.h"
#include "common/vsoc/shm/version.h"
#include "uapi/vsoc_shm.h"

// Memory layout for the hwcomposer and hwcomposer broadcast regions

namespace vsoc {
namespace layout {

namespace framebuffer {

struct FBBroadcastLayout : public RegionLayout {
  static const char* region_name;
  // Display properties
  uint32_t x_res;
  uint32_t y_res;
  uint16_t dpi;
  uint16_t refresh_rate_hz;

  // The frame sequential number
  uint32_t seq_num;
  // The offset in the gralloc buffer region of the current frame buffer.
  vsoc_reg_off_t frame_offset;
  // Protects access to the frame offset and sequential number.
  // See the region implementation for more details.
  SpinLock bcast_lock;
};
ASSERT_SHM_COMPATIBLE(FBBroadcastLayout, framebuffer);

}  // namespace framebuffer

}  // namespace layout
}  // namespace vsoc

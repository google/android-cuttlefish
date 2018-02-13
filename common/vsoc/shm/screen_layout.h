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

// Memory layout for screen region

namespace vsoc {
namespace layout {

namespace screen {
struct TimeSpec {
  int64_t ts_sec;
  uint32_t ts_nsec;
  // Host and guest compilers are giving the structure different sizes without
  // this field.
  uint32_t reserved;
};
struct CompositionStats {
  uint32_t num_prepare_calls;
  uint16_t num_layers;
  uint16_t num_hwcomposited_layers;
  TimeSpec last_vsync;
  TimeSpec prepare_start;
  TimeSpec prepare_end;
  TimeSpec set_start;
  TimeSpec set_end;
};

struct ScreenLayout : public RegionLayout {
  static const char* region_name;
  // Display properties
  uint32_t x_res;
  uint32_t y_res;
  uint16_t dpi;
  uint16_t refresh_rate_hz;

  // Protects access to the frame offset, sequential number and stats.
  // See the region implementation for more details.
  SpinLock bcast_lock;
  // The frame sequential number
  std::atomic<uint32_t> seq_num;
  // The index of the buffer containing the current frame.
  int32_t buffer_index;
  CompositionStats stats;
  uint8_t buffer[0];
};
ASSERT_SHM_COMPATIBLE(ScreenLayout, screen);

}  // namespace screen

}  // namespace layout
}  // namespace vsoc

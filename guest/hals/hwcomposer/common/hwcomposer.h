#pragma once
/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <hardware/hwcomposer.h>

namespace cvd {
  struct hwc_composer_device_data_t {
    const hwc_procs_t* procs;
    pthread_t vsync_thread;
    int64_t vsync_base_timestamp;
    int32_t vsync_period_ns;
  };

  void* hwc_vsync_thread(void* data);
}  // namespace cvd

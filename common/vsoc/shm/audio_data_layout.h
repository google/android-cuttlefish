#pragma once
/*
 * Copyright (C) 2018 The Android Open Source Project
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
#include "common/vsoc/shm/circqueue.h"

// Memory layout for region carrying audio data from audio HAL to client.

namespace vsoc {
namespace layout {
namespace audio_data {

struct AudioDataLayout : public RegionLayout {
    static const char *const region_name;

    // size = 2^14 = 16384, packets are up to 4KB bytes each.
    CircularPacketQueue<14, 4096> audio_queue;
};

}  // namespace audio_data
}  // namespace layout
}  // namespace vsoc

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

#include "common/vsoc/lib/audio_data_region_view.h"

#include "common/vsoc/lib/circqueue_impl.h"

#include <mutex>

using vsoc::layout::audio_data::AudioDataLayout;

namespace vsoc {
namespace audio_data {

#if defined(CUTTLEFISH_HOST)
std::shared_ptr<AudioDataRegionView> AudioDataRegionView::GetInstance(
    const char* domain) {
  return RegionView::GetInstanceImpl<AudioDataRegionView>(
      [](std::shared_ptr<AudioDataRegionView> region, const char* domain) {
        return region->Open(domain);
      },
      domain);
}
#else
std::shared_ptr<AudioDataRegionView> AudioDataRegionView::GetInstance() {
  return RegionView::GetInstanceImpl<AudioDataRegionView>(
      std::mem_fn(&AudioDataRegionView::Open));
}
#endif
}  // namespace audio_data
}  // namespace vsoc

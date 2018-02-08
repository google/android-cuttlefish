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

#include "common/vsoc/lib/fb_bcast_region_view.h"

#include "common/libs/glog/logging.h"
#include "common/vsoc/lib/lock_guard.h"

using vsoc::framebuffer::FBBroadcastRegionView;
using vsoc::layout::framebuffer::CompositionStats;

// We can use a locking protocol because we decided that the streamer should
// have more priority than the hwcomposer, so it's OK to block the hwcomposer
// waiting for the streamer to complete, while the streamer will only block on
// the hwcomposer when it's ran out of work to do and needs to get more from the
// hwcomposer.
void FBBroadcastRegionView::BroadcastNewFrame(vsoc_reg_off_t frame_offset,
                                              const CompositionStats* stats) {
  {
    auto lock_guard(make_lock_guard(&data()->bcast_lock));
    data()->seq_num++;
    data()->frame_offset = frame_offset;
    if (stats) {
      data()->stats = *stats;
    }
  }
  // Signaling after releasing the lock may cause spurious wake ups.
  // Signaling while holding the lock may cause the just-awaken listener to
  // block immediately trying to acquire the lock.
  // The former is less costly and slightly less likely to happen.
  layout::Sides side;
  side.value_ = layout::Sides::Both;
  SendSignal(side, &data()->seq_num);
}

vsoc_reg_off_t FBBroadcastRegionView::WaitForNewFrameSince(
    uint32_t* last_seq_num, CompositionStats* stats) {
  static std::unique_ptr<RegionWorker> worker = StartWorker();
  // It's ok to read seq_num here without holding the lock because the lock will
  // be acquired immediately after so we'll block if necessary to wait for the
  // critical section in BroadcastNewFrame to complete.
  // Also, the call to WaitForSignal receives a pointer to seq_num (so the
  // compiler should not optimize it out) and includes a memory barrier
  // (FUTEX_WAIT).
  while (data()->seq_num == *last_seq_num) {
    // Don't hold the lock when waiting for a signal, will deadlock.
    WaitForSignal(&data()->seq_num, *last_seq_num);
  }

  {
    auto lock_guard(make_lock_guard(&data()->bcast_lock));
    *last_seq_num = data()->seq_num;
    if (stats) {
      *stats = data()->stats;
    }
    return data()->frame_offset;
  }
}

#if defined(CUTTLEFISH_HOST)
std::shared_ptr<FBBroadcastRegionView> FBBroadcastRegionView::GetInstance(
    const char* domain) {
  return RegionView::GetInstanceImpl<FBBroadcastRegionView>(
      [](std::shared_ptr<FBBroadcastRegionView> region, const char* domain) {
        return region->Open(domain);
      },
      domain);
}
#else
std::shared_ptr<FBBroadcastRegionView> FBBroadcastRegionView::GetInstance() {
  return RegionView::GetInstanceImpl<FBBroadcastRegionView>(
      std::mem_fn(&FBBroadcastRegionView::Open));
}
#endif

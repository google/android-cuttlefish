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

#include "common/vsoc/framebuffer/fb_bcast_region.h"

#include "common/libs/glog/logging.h"
#include "common/vsoc/lib/lock_guard.h"

using vsoc::framebuffer::FBBroadcastRegion;
using vsoc::layout::GuestAndHostLock;

FBBroadcastRegion* FBBroadcastRegion::GetInstance() {
  static FBBroadcastRegion region;
  if (!region.is_open_) {
    LOG(FATAL) << "Unable to open framebuffer broadcast region";
    return nullptr;
  }
  return &region;
}

// We can use a locking protocol because we decided that the streamer should
// have more priority than the hwcomposer, so it's OK to block the hwcomposer
// waiting for the streamer to complete, while the streamer will only block on
// the hwcomposer when it's ran out of work to do and needs to get more from the
// hwcomposer.
void FBBroadcastRegion::BroadcastNewFrame(uint32_t seq_num,
                                          vsoc_reg_off_t frame_offset) {
  {
    GuestAndHostLockGuard<GuestAndHostLock, FBBroadcastRegion> lock_guard(
        &data()->bcast_lock, this);
    data()->seq_num = seq_num;
    data()->frame_offset = frame_offset;
  }
  // Signaling after releasing the lock may cause spurious wake ups.
  // Signaling while holding the lock may cause the just-awaken listener to
  // block immediately trying to acquire the lock.
  // The former is less costly and slightly less likely to happen.
  layout::Sides side;
  side.value_ = layout::Sides::Both;
  SendSignal(side, &data()->seq_num);
}

vsoc_reg_off_t FBBroadcastRegion::WaitForNewFrameSince(uint32_t* last_seq_num) {
  static std::unique_ptr<RegionWorker> worker = StartWorker();
  // It's ok to read seq_num here without holding the lock because the lock will
  // be acquired immediately after so we'll block if necessary to wait for the
  // critical section in BroadcastNewFrame to complete.
  // Also, the call to WaitForSignal receives a pointer to seq_num (so the
  // compiler should not optimize it out) and includes a memory barrier
  // (FUTEX_WAIT).
  while(data()->seq_num == *last_seq_num) {
    // Don't hold the lock when waiting for a signal, will deadlock.
    WaitForSignal(&data()->seq_num, *last_seq_num);
  }

  {
    GuestAndHostLockGuard<GuestAndHostLock, FBBroadcastRegion> lock_guard(
        &data()->bcast_lock, this);
    *last_seq_num = data()->seq_num;
    return data()->frame_offset;
  }
}

FBBroadcastRegion::FBBroadcastRegion() : properties_(this) {
  // Open here since the constructor in the singleton is thread safe.
  // TODO(jemoreira): Get the domain from somewhere
  is_open_ = Open(nullptr);
}

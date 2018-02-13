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

#include "common/vsoc/lib/screen_region_view.h"

#include <memory>

#include "common/libs/glog/logging.h"
#include "common/vsoc/lib/lock_guard.h"

using vsoc::layout::screen::CompositionStats;
using vsoc::screen::ScreenRegionView;

const uint8_t* ScreenRegionView::first_buffer() const {
  // TODO(jemoreira): Add alignments?
  return &(this->data().buffer[0]);
}

int ScreenRegionView::number_of_buffers() const {
  auto offset_of_first_buffer =
      const_cast<ScreenRegionView*>(this)->pointer_to_region_offset(
          this->first_buffer());
  size_t total_buffer_size = control_->region_size() - offset_of_first_buffer;
  return total_buffer_size / buffer_size();
}

void* ScreenRegionView::GetBuffer(int buffer_idx) {
  uint8_t* buffer = const_cast<uint8_t*>(this->first_buffer());
  return buffer + buffer_idx * this->buffer_size();
}

// We can use a locking protocol because we decided that the streamer should
// have more priority than the hwcomposer, so it's OK to block the hwcomposer
// waiting for the streamer to complete, while the streamer will only block on
// the hwcomposer when it has ran out of work to do and needs to get more from
// the hwcomposer.
void ScreenRegionView::BroadcastNewFrame(int buffer_idx,
                                         const CompositionStats* stats) {
  {
    if (buffer_idx < 0 || buffer_idx >= number_of_buffers()) {
      LOG(ERROR) << "Attempting to broadcast an invalid buffer index: "
                 << buffer_idx;
      return;
    }
    auto lock_guard(make_lock_guard(&data()->bcast_lock));
    data()->seq_num++;
    data()->buffer_index = static_cast<int32_t>(buffer_idx);
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

int ScreenRegionView::WaitForNewFrameSince(uint32_t* last_seq_num,
                                           CompositionStats* stats) {
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
    return static_cast<int>(data()->buffer_index);
  }
}

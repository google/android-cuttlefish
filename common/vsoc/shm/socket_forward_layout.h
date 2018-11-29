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
#pragma once

#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/circqueue.h"
#include "common/vsoc/shm/lock.h"

// Memory layout for wifi packet exchange region.
namespace vsoc {
namespace layout {
namespace socket_forward {

constexpr std::size_t kMaxPacketSize = 8192;
constexpr std::size_t kNumQueues = 16;

struct Queue {
  static constexpr size_t layout_size =
      CircularPacketQueue<16, kMaxPacketSize>::layout_size;

  CircularPacketQueue<16, kMaxPacketSize> queue;

  bool Recover() { return queue.Recover(); }
};
ASSERT_SHM_COMPATIBLE(Queue);

struct QueuePair {
  static constexpr size_t layout_size = 2 * Queue::layout_size;

  // Traffic originating from host that proceeds towards guest.
  Queue host_to_guest;
  // Traffic originating from guest that proceeds towards host.
  Queue guest_to_host;

  bool Recover() {
    bool recovered = false;
    recovered = recovered || host_to_guest.Recover();
    recovered = recovered || guest_to_host.Recover();
    return recovered;
  }
};
ASSERT_SHM_COMPATIBLE(QueuePair);

struct SocketForwardLayout : public RegionLayout {
  static constexpr size_t layout_size = QueuePair::layout_size * kNumQueues;

  bool Recover() {
    bool recovered = false;
    for (auto& i : queues_) {
      bool rval = i.Recover();
      recovered = recovered || rval;
    }
    return recovered;
  }

  QueuePair queues_[kNumQueues];
  static const char* region_name;
};

ASSERT_SHM_COMPATIBLE(SocketForwardLayout);

}  // namespace socket_forward
}  // namespace layout
}  // namespace vsoc

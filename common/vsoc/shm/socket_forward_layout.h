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

namespace QueueState {
constexpr std::uint32_t INACTIVE = 0;
constexpr std::uint32_t HOST_CONNECTED = 1;
constexpr std::uint32_t BOTH_CONNECTED = 2;
constexpr std::uint32_t HOST_CLOSED = 3;
constexpr std::uint32_t GUEST_CLOSED = 4;
// If both are closed then the queue goes back to INACTIVE
// BOTH_CLOSED = 0,
}  // namespace QueueState

struct Queue {
  static constexpr size_t layout_size =
      CircularPacketQueue<16, kMaxPacketSize>::layout_size + 4;

  CircularPacketQueue<16, kMaxPacketSize> queue;

  std::atomic_uint32_t queue_state_;

  bool Recover() { return queue.Recover(); }
};
ASSERT_SHM_COMPATIBLE(Queue);

struct QueuePair {
  static constexpr size_t layout_size = 2 * Queue::layout_size + 8;

  // Traffic originating from host that proceeds towards guest.
  Queue host_to_guest;
  // Traffic originating from guest that proceeds towards host.
  Queue guest_to_host;

  std::uint32_t port_;

  SpinLock queue_state_lock_;

  bool Recover() {
    // TODO: Put queue_state_ and port_ recovery here, probably after grabbing
    bool recovered = false;
    recovered = recovered || host_to_guest.Recover();
    recovered = recovered || guest_to_host.Recover();
    recovered = recovered || queue_state_lock_.Recover();
    return recovered;
  }
};
ASSERT_SHM_COMPATIBLE(QueuePair);

struct SocketForwardLayout : public RegionLayout {
  static constexpr size_t layout_size = QueuePair::layout_size * kNumQueues + 8;

  bool Recover() {
    bool recovered = false;
    for (auto& i : queues_) {
      bool rval = i.Recover();
      recovered = recovered || rval;
    }
    // TODO: consider handling the sequence number here
    return recovered;
  }

  QueuePair queues_[kNumQueues];
  std::atomic_uint32_t seq_num;  // incremented for every new connection
  std::atomic_uint32_t
      generation_num;  // incremented for every new socket forward process
  static const char* region_name;
};

ASSERT_SHM_COMPATIBLE(SocketForwardLayout);

}  // namespace socket_forward
}  // namespace layout
}  // namespace vsoc

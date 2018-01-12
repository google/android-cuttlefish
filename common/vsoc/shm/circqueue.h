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

// Memory layout for byte-oriented circular queues

#include <atomic>
#include <cstdint>

#include "common/vsoc/shm/base.h"
#include "common/vsoc/shm/lock.h"

namespace vsoc {
class RegionSignalingInterface;
namespace layout {

/**
 * Base classes for all spinlock protected circular queues.
 * This class should be embedded in the per-region data structure that is used
 * as the parameter to TypedRegion.
 */
template <uint32_t SizeLog2>
class CircularQueueBase {
  CircularQueueBase() = delete;
  CircularQueueBase(const CircularQueueBase&) = delete;
  CircularQueueBase& operator=(const CircularQueueBase&) = delete;

 protected:
  /**
   * Specifies a part of the queue. Note, the given indexes must be masked
   * before they can be used against buffer_
   */
  struct Range {
    // Points to the first bytes that is part of the range
    uint32_t start_idx;
    // Points to the first byte that is not in the range. This is similar to
    // the STL end iterator.
    uint32_t end_idx;
  };
  static const uintptr_t BufferSize = (1 << SizeLog2);

  /**
   * Copy bytes from buffer_in into the part of the queue specified by Range.
   */
  void CopyInRange(const char* buffer_in, const Range& t);

  /**
   * Copy the bytes specified by range to the given buffer. They caller must
   * ensure that the buffer is large enough to hold the content of the range.
   */
  void CopyOutRange(const Range& t, char* buffer_out);

  /**
   * Wait until data becomes available in the queue. The caller must have
   * called Lock() before invoking this. The caller must call Unlock()
   * after this returns.
   */
  void WaitForDataLocked(RegionSignalingInterface* r);

  /**
   * Reserve space in the queue for writing. The caller must have called Lock()
   * before invoking this. The caller must call Unlock() after this returns.
   * Indexes pointing to the reserved space will be placed in range.
   * On success this returns bytes.
   * On failure a negative errno indicates the problem. -ENOSPC indicates that
   * bytes > the queue size, -EWOULDBLOCK indicates that the call would block
   * waiting for space but was requested non bloking.
   */
  intptr_t WriteReserveLocked(RegionSignalingInterface* r, size_t bytes,
                              Range* t, bool non_blocking);

  // Note: Both of these fields may hold values larger than the buffer size,
  // they should be interpreted modulo the buffer size. This fact along with the
  // buffer size being a power of two greatly simplyfies the index calculations.
  // Advances when a reader has finished with buffer space
  uint32_t r_released_;
  // Advances when buffer space is filled and ready for a reader
  uint32_t w_pub_;
  // Spinlock that protects the region. 0 means unlocked
  SpinLock lock_;
  // The actual memory in the buffer
  char buffer_[BufferSize];
};
using CircularQueueBase64k = CircularQueueBase<16>;
ASSERT_SHM_COMPATIBLE(CircularQueueBase64k, multi_region);

/**
 * Byte oriented circular queue. Reads will always return some data, but
 * may return less data than requested. Writes will always write all of the
 * data or return an error.
 */
template <uint32_t SizeLog2>
class CircularByteQueue : public CircularQueueBase<SizeLog2> {
 public:
  /**
   * Read at most max_size bytes from the qeueue, placing them in buffer_out
   */
  intptr_t Read(RegionSignalingInterface* r, char* buffer_out,
                std::size_t max_size);
  /**
   * Write all of the given bytes into the queue. If non_blocking isn't set the
   * call may block until there is enough available space in the queue. On
   * success the return value will match bytes. On failure a negative errno is
   * returned. -ENOSPC: If the queue size is smaller than the number of bytes to
   * write. -EWOULDBLOCK: If non_blocking is true and there is not enough free
   * space.
   */
  intptr_t Write(RegionSignalingInterface* r, const char* buffer_in,
                 std::size_t bytes, bool non_blocking = false);

 protected:
  using Range = typename CircularQueueBase<SizeLog2>::Range;
};
using CircularByteQueue64k = CircularByteQueue<16>;
ASSERT_SHM_COMPATIBLE(CircularByteQueue64k, multi_region);

/**
 * Packet oriented circular queue. Reads will either return data or an error.
 * Each return from read corresponds to a call to write and returns all of the
 * data from that corresponding Write().
 */
template <uint32_t SizeLog2, uint32_t MaxPacketSize>
class CircularPacketQueue : public CircularQueueBase<SizeLog2> {
 public:
  /**
   * Read a single packet from the queue, placing its data into buffer_out.
   * If max_size indicates that buffer_out cannot hold the entire packet
   * this function will return -ENOSPC.
   */
  intptr_t Read(RegionSignalingInterface* r, char* buffer_out,
                std::size_t max_size);

  /**
   * Writes [buffer_in, buffer_in + bytes) to the queue.
   * If the number of bytes to be written exceeds the size of the queue
   * -ENOSPC will be returned.
   * If non_blocking is true and there is not enough free space on the queue to
   * write all the data -EWOULDBLOCK will be returned.
   */
  intptr_t Write(RegionSignalingInterface* r, const char* buffer_in,
                 uint32_t bytes, bool non_blocking = false);

 protected:
  static_assert(CircularQueueBase<SizeLog2>::BufferSize >= MaxPacketSize,
                "Buffer is too small to hold the maximum sized packet");
  using Range = typename CircularQueueBase<SizeLog2>::Range;
  intptr_t CalculateBufferedSize(size_t payload);
};
using CircularPacketQueue64k = CircularPacketQueue<16, 1024>;
ASSERT_SHM_COMPATIBLE(CircularPacketQueue64k, multi_region);

}  // namespace layout
}  // namespace vsoc

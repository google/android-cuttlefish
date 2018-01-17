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

#include <cerrno>
#include <cstring>

#include "common/vsoc/lib/region_signaling_interface.h"
#include "common/vsoc/shm/circqueue.h"

namespace {
// Increases the given index until it is naturally aligned for T.
template <typename T>
uintptr_t align(uintptr_t index) {
  return (index + sizeof(T) - 1) & ~(sizeof(T) - 1);
}
}  // namespace

namespace vsoc {
class RegionSignalingInterface;
namespace layout {

template <uint32_t SizeLog2>
void CircularQueueBase<SizeLog2>::CopyInRange(const char* buffer_in,
                                              const Range& t) {
  size_t bytes = t.end_idx - t.start_idx;
  uint32_t index = t.start_idx & (BufferSize - 1);
  if (index + bytes <= BufferSize) {
    std::memcpy(buffer_ + index, buffer_in, bytes);
  } else {
    size_t part1_size = BufferSize - index;
    size_t part2_size = bytes - part1_size;
    std::memcpy(buffer_ + index, buffer_in, part1_size);
    std::memcpy(buffer_, buffer_in + part1_size, part2_size);
  }
}

template <uint32_t SizeLog2>
void CircularQueueBase<SizeLog2>::CopyOutRange(const Range& t,
                                               char* buffer_out) {
  uint32_t index = t.start_idx & (BufferSize - 1);
  size_t total_size = t.end_idx - t.start_idx;
  if (index + total_size <= BufferSize) {
    std::memcpy(buffer_out, buffer_ + index, total_size);
  } else {
    uint32_t part1_size = BufferSize - index;
    uint32_t part2_size = total_size - part1_size;
    std::memcpy(buffer_out, buffer_ + index, part1_size);
    std::memcpy(buffer_out + part1_size, buffer_, part2_size);
  }
}

template <uint32_t SizeLog2>
void CircularQueueBase<SizeLog2>::WaitForDataLocked(
    RegionSignalingInterface* r) {
  while (1) {
    uint32_t o_w_pub = w_pub_;
    // We don't have data. Wait until some appears and try again
    if (r_released_ != o_w_pub) {
      return;
    }
    lock_.Unlock();
    r->WaitForSignal(&w_pub_, o_w_pub);
    lock_.Lock();
  }
}

template <uint32_t SizeLog2>
intptr_t CircularQueueBase<SizeLog2>::WriteReserveLocked(
    RegionSignalingInterface* r, size_t bytes, Range* t, bool non_blocking) {
  // Can't write more than the buffer will hold
  if (bytes > BufferSize) {
    return -ENOSPC;
  }
  while (true) {
    uint32_t o_w_pub = w_pub_;
    uint32_t o_r_release = r_released_;
    uint32_t bytes_in_use = o_w_pub - o_r_release;
    size_t available = BufferSize - bytes_in_use;
    if (available >= bytes) {
      t->start_idx = o_w_pub;
      t->end_idx = o_w_pub + bytes;
      break;
    }
    if (non_blocking) {
      return -EWOULDBLOCK;
    }
    // If we can't write at the moment wait for a reader to release
    // some bytes.
    lock_.Unlock();
    r->WaitForSignal(&r_released_, o_r_release);
    lock_.Lock();
  }
  return t->end_idx - t->start_idx;
}

template <uint32_t SizeLog2>
intptr_t CircularByteQueue<SizeLog2>::Read(RegionSignalingInterface* r,
                                           char* buffer_out, size_t max_size) {
  this->lock_.Lock();
  this->WaitForDataLocked(r);
  Range t;
  t.start_idx = this->r_released_;
  t.end_idx = this->w_pub_;
  // The lock is still held here...
  // Trim the range if we got more than the reader wanted
  if ((t.end_idx - t.start_idx) > max_size) {
    t.end_idx = t.start_idx + max_size;
  }
  this->CopyOutRange(t, buffer_out);
  this->r_released_ = t.end_idx;
  this->lock_.Unlock();
  layout::Sides side;
  side.value_ = layout::Sides::Both;
  r->SendSignal(side, &this->r_released_);
  return t.end_idx - t.start_idx;
}

template <uint32_t SizeLog2>
intptr_t CircularByteQueue<SizeLog2>::Write(RegionSignalingInterface* r,
                                            const char* buffer_in, size_t bytes,
                                            bool non_blocking) {
  Range range;
  this->lock_.Lock();
  intptr_t rval = this->WriteReserveLocked(r, bytes, &range, non_blocking);
  if (rval < 0) {
    this->lock_.Unlock();
    return rval;
  }
  this->CopyInRange(buffer_in, range);
  // We can't publish until all of the previous write allocations where
  // published.
  this->w_pub_ = range.end_idx;
  this->lock_.Unlock();
  layout::Sides side;
  side.value_ = layout::Sides::Both;
  r->SendSignal(side, &this->w_pub_);
  return bytes;
}

template <uint32_t SizeLog2, uint32_t MaxPacketSize>
intptr_t CircularPacketQueue<SizeLog2, MaxPacketSize>::CalculateBufferedSize(
    size_t payload) {
  return align<uint32_t>(sizeof(uint32_t) + payload);
}

template <uint32_t SizeLog2, uint32_t MaxPacketSize>
intptr_t CircularPacketQueue<SizeLog2, MaxPacketSize>::Read(
    RegionSignalingInterface* r, char* buffer_out, size_t max_size) {
  this->lock_.Lock();
  this->WaitForDataLocked(r);
  uint32_t packet_size = *reinterpret_cast<uint32_t*>(
      this->buffer_ + (this->r_released_ & (this->BufferSize - 1)));
  if (packet_size > max_size) {
    this->lock_.Unlock();
    return -ENOSPC;
  }
  Range t;
  t.start_idx = this->r_released_ + sizeof(uint32_t);
  t.end_idx = t.start_idx + packet_size;
  this->CopyOutRange(t, buffer_out);
  this->r_released_ += this->CalculateBufferedSize(packet_size);
  this->lock_.Unlock();
  layout::Sides side;
  side.value_ = layout::Sides::Both;
  r->SendSignal(side, &this->r_released_);
  return packet_size;
}

template <uint32_t SizeLog2, uint32_t MaxPacketSize>
intptr_t CircularPacketQueue<SizeLog2, MaxPacketSize>::Write(
    RegionSignalingInterface* r, const char* buffer_in, uint32_t bytes,
    bool non_blocking) {
  iovec iov;
  iov.iov_base = const_cast<char *>(buffer_in);
  iov.iov_len = bytes;
  return Writev(r, &iov, 1 /* iov_count */, non_blocking);
}

template <uint32_t SizeLog2, uint32_t MaxPacketSize>
intptr_t CircularPacketQueue<SizeLog2, MaxPacketSize>::Writev(
      RegionSignalingInterface *r,
      const iovec *iov,
      size_t iov_count,
      bool non_blocking) {
  size_t bytes = 0;
  for (size_t i = 0; i < iov_count; ++i) {
    bytes += iov[i].iov_len;
  }

  if (bytes > MaxPacketSize) {
    return -ENOSPC;
  }

  Range range;
  size_t buffered_size = this->CalculateBufferedSize(bytes);
  this->lock_.Lock();
  intptr_t rval =
      this->WriteReserveLocked(r, buffered_size, &range, non_blocking);
  if (rval < 0) {
    this->lock_.Unlock();
    return rval;
  }
  Range header = range;
  header.end_idx = header.start_idx + sizeof(uint32_t);
  Range payload{
      static_cast<uint32_t>(range.start_idx + sizeof(uint32_t)),
      static_cast<uint32_t>(range.start_idx + sizeof(uint32_t) + bytes)};
  this->CopyInRange(reinterpret_cast<const char*>(&bytes), header);

  Range subRange = payload;
  for (size_t i = 0; i < iov_count; ++i) {
    subRange.end_idx = subRange.start_idx + iov[i].iov_len;
    this->CopyInRange(static_cast<const char *>(iov[i].iov_base), subRange);

    subRange.start_idx = subRange.end_idx;
  }

  this->w_pub_ = range.end_idx;
  this->lock_.Unlock();
  layout::Sides side;
  side.value_ = layout::Sides::Both;
  r->SendSignal(side, &this->w_pub_);
  return bytes;
}

}  // namespace layout
}  // namespace vsoc

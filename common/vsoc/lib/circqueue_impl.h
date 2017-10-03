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

#include "common/vsoc/lib/region_view.h"
#include "common/vsoc/shm/circqueue.h"

namespace {
// Increases the given index until it is naturally aligned for T.
template <typename T>
uintptr_t align(uintptr_t index) {
  return (index + sizeof(T) - 1) & ~(sizeof(T) - 1);
}
}  // namespace

namespace vsoc {
class RegionView;
namespace layout {

template <uint32_t SizeLog2>
void CircularQueueBase<SizeLog2>::CopyInRange(const char* buffer_in,
                                              const Range& t) {
  size_t bytes = t.end_idx - t.start_idx;
  uint32_t index = t.start_idx & (BufferSize - 1);
  if (index + bytes < BufferSize) {
    memcpy(buffer_ + index, buffer_in, bytes);
  } else {
    size_t part1_size = BufferSize - index;
    size_t part2_size = bytes - part1_size;
    memcpy(buffer_ + index, buffer_in, part1_size);
    memcpy(buffer_, buffer_in + part1_size, part2_size);
  }
}

template <uint32_t SizeLog2>
void CircularQueueBase<SizeLog2>::CopyOutRange(const Range& t,
                                               char* buffer_out) {
  uint32_t index = t.start_idx & (BufferSize - 1);
  size_t total_size = t.end_idx - t.start_idx;
  if (index + total_size <= BufferSize) {
    memcpy(buffer_out, buffer_ + index, total_size);
  } else {
    uint32_t part1_size = BufferSize - index;
    uint32_t part2_size = total_size - part1_size;
    memcpy(buffer_out, buffer_ + index, part1_size);
    memcpy(buffer_out + part1_size, buffer_, part2_size);
  }
}

template <uint32_t SizeLog2>
void CircularQueueBase<SizeLog2>::WaitForDataLocked(RegionView* r) {
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
intptr_t CircularQueueBase<SizeLog2>::WriteReserveLocked(RegionView* r,
                                                         size_t bytes,
                                                         Range* t) {
  // Can't write more than the buffer will hold
  if (bytes > BufferSize) {
    return -ENOSPC;
  }
  while (true) {
    t->start_idx = w_pub_;
    uint32_t o_r_release = r_released_;
    size_t available = BufferSize - t->start_idx + o_r_release;
    if (available >= bytes) {
      break;
    }
    // If we can't write at the moment wait for a reader to release
    // some bytes.
    lock_.Unlock();
    r->WaitForSignal(&r_released_, o_r_release);
    lock_.Lock();
  }
  t->end_idx = t->start_idx + bytes;
  return t->end_idx - t->start_idx;
}

template <uint32_t SizeLog2>
intptr_t CircularByteQueue<SizeLog2>::Read(RegionView* r, char* buffer_out,
                                           size_t max_size) {
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
  this->CopyOutRange(t, buffer_out, max_size);
  this->r_released_ = t.end_idx;
  this->lock_.Unlock();
  r->SendSignal(layout::Sides::Both, &this->r_released_);
  return t->end_idx - t->start_idx;
}

template <uint32_t SizeLog2>
intptr_t CircularByteQueue<SizeLog2>::Write(RegionView* r,
                                            const char* buffer_in,
                                            size_t bytes) {
  Range range;
  this->lock_.Lock();
  intptr_t rval = WriteReserveLocked(r, bytes, &range);
  if (rval < 0) {
    this->lock_.Unlock();
    return rval;
  }
  this->CopyInRange(buffer_in, range);
  // We can't publish until all of the previous write allocations where
  // published.
  this->w_pub_ = range.end_idx;
  this->lock_.Unlock();
  r->SendSignal(layout::Sides::Both, &this->w_pub_);
  return bytes;
}

template <uint32_t SizeLog2, uint32_t MaxPacketSize>
intptr_t CircularPacketQueue<SizeLog2, MaxPacketSize>::CalculateBufferedSize(
    size_t payload) {
  return align<uint32_t>(sizeof(uint32_t) + payload);
}

template <uint32_t SizeLog2, uint32_t MaxPacketSize>
intptr_t CircularPacketQueue<SizeLog2, MaxPacketSize>::Read(RegionView* r,
                                                            char* buffer_out,
                                                            size_t max_size) {
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
    RegionView* r, const char* buffer_in, uint32_t bytes) {
  if (bytes > MaxPacketSize) {
    return -ENOSPC;
  }
  Range range;
  size_t buffered_size = this->CalculateBufferedSize(bytes);
  this->lock_.Lock();
  intptr_t rval = this->WriteReserveLocked(r, buffered_size, &range);
  if (rval < 0) {
    this->lock_.Unlock();
    return rval;
  }
  Range header = range;
  header.end_idx = header.start_idx + sizeof(uint32_t);
  Range payload{range.start_idx + sizeof(uint32_t),
                range.start_idx + sizeof(uint32_t) + bytes};
  this->CopyInRange(reinterpret_cast<const char*>(&bytes), header);
  this->CopyInRange(buffer_in, payload);
  this->w_pub_ = range.end_idx;
  this->lock_.Unlock();
  layout::Sides side;
  side.value_ = layout::Sides::Both;
  r->SendSignal(side, &this->w_pub_);
  return bytes;
}

}  // namespace layout
}  // namespace vsoc

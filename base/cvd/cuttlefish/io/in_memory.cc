//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/io/in_memory.h"

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

InMemoryIo::InMemoryIo(std::vector<char> data) : data_(std::move(data)) {}

Result<uint64_t> InMemoryIo::Read(void* buf, uint64_t count) {
  std::lock_guard lock(mutex_);
  uint64_t to_read = ClampRange(cursor_, count);
  memcpy(buf, &data_[cursor_], to_read);
  cursor_ += to_read;
  return to_read;
}

Result<uint64_t> InMemoryIo::Write(const void* buf, uint64_t count) {
  std::lock_guard lock(mutex_);
  GrowTo(cursor_ + count);
  memcpy(&data_[cursor_], buf, count);
  cursor_ += count;
  return count;
}

Result<uint64_t> InMemoryIo::SeekSet(uint64_t offset) {
  std::lock_guard lock(mutex_);
  GrowTo(offset);
  return cursor_ = offset;
}

Result<uint64_t> InMemoryIo::SeekCur(int64_t offset) {
  std::lock_guard lock(mutex_);
  uint64_t new_pos =
      std::max<int64_t>(static_cast<int64_t>(cursor_) + offset, 0);
  GrowTo(new_pos);
  return cursor_ = new_pos;
}

Result<uint64_t> InMemoryIo::SeekEnd(int64_t offset) {
  std::lock_guard lock(mutex_);
  uint64_t new_pos =
      std::max<int64_t>(static_cast<int64_t>(data_.size()) + offset, 0);
  GrowTo(new_pos);
  return cursor_ = new_pos;
}

Result<uint64_t> InMemoryIo::PRead(void* buf, uint64_t count,
                                   uint64_t offset) const {
  std::shared_lock lock(mutex_);
  uint64_t to_read = ClampRange(offset, count);
  memcpy(buf, &data_[offset], to_read);
  return to_read;
}

Result<uint64_t> InMemoryIo::PWrite(const void* buf, uint64_t count,
                                    uint64_t offset) {
  std::lock_guard lock(mutex_);
  GrowTo(offset + count);
  memcpy(&data_[offset], buf, count);
  return count;
}

// Must be called with the lock held for reading or writing
uint64_t InMemoryIo::ClampRange(uint64_t begin, uint64_t length) const {
  if (begin > data_.size()) {
    return 0;
  }
  return std::min(length, data_.size() - begin);
}

// Must be called with the lock held for writing
void InMemoryIo::GrowTo(uint64_t new_size) {
  if (data_.size() < new_size) {
    data_.resize(new_size, '\0');
  }
}

}  // namespace cuttlefish

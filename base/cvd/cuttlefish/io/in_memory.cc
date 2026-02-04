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

Result<size_t> InMemoryIo::PartialRead(void* buf, size_t count) {
  std::lock_guard lock(mutex_);
  size_t to_read = ClampRange(cursor_, count);
  memcpy(buf, &data_[cursor_], to_read);
  cursor_ += to_read;
  return to_read;
}

Result<size_t> InMemoryIo::PartialWrite(const void* buf, size_t count) {
  std::lock_guard lock(mutex_);
  GrowTo(cursor_ + count);
  memcpy(&data_[cursor_], buf, count);
  cursor_ += count;
  return count;
}

Result<size_t> InMemoryIo::SeekSet(size_t offset) {
  std::lock_guard lock(mutex_);
  GrowTo(offset);
  return cursor_ = offset;
}

Result<size_t> InMemoryIo::SeekCur(ssize_t offset) {
  std::lock_guard lock(mutex_);
  size_t new_pos = std::max<ssize_t>(static_cast<ssize_t>(cursor_) + offset, 0);
  GrowTo(new_pos);
  return cursor_ = new_pos;
}

Result<size_t> InMemoryIo::SeekEnd(ssize_t offset) {
  std::lock_guard lock(mutex_);
  size_t new_pos =
      std::max<ssize_t>(static_cast<ssize_t>(data_.size()) + offset, 0);
  GrowTo(new_pos);
  return cursor_ = new_pos;
}

Result<size_t> InMemoryIo::PartialReadAt(void* buf, size_t count,
                                         size_t offset) const {
  std::shared_lock lock(mutex_);
  size_t to_read = ClampRange(offset, count);
  memcpy(buf, &data_[offset], to_read);
  return to_read;
}

Result<size_t> InMemoryIo::PartialWriteAt(const void* buf, size_t count,
                                          size_t offset) {
  std::lock_guard lock(mutex_);
  GrowTo(offset + count);
  memcpy(&data_[offset], buf, count);
  return count;
}

// Must be called with the lock held for reading or writing
size_t InMemoryIo::ClampRange(size_t begin, size_t length) const {
  if (begin > data_.size()) {
    return 0;
  }
  return std::min(length, data_.size() - begin);
}

// Must be called with the lock held for writing
void InMemoryIo::GrowTo(size_t new_size) {
  if (data_.size() < new_size) {
    data_.resize(new_size, '\0');
  }
}

}  // namespace cuttlefish

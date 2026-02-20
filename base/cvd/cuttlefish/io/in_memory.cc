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
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

class InMemoryIo : public ReaderWriterSeeker {
 public:
  InMemoryIo(std::vector<char>& data, std::shared_mutex& mutex)
      : data_(data), mutex_(mutex) {}

  Result<uint64_t> Read(void* buf, uint64_t count) override {
    std::lock_guard lock(mutex_);
    uint64_t to_read = ClampRange(cursor_, count);
    memcpy(buf, &data_[cursor_], to_read);
    cursor_ += to_read;
    return to_read;
  }

  Result<uint64_t> Write(const void* buf, uint64_t count) override {
    std::lock_guard lock(mutex_);
    GrowTo(cursor_ + count);
    memcpy(&data_[cursor_], buf, count);
    cursor_ += count;
    return count;
  }

  Result<uint64_t> SeekSet(uint64_t offset) override {
    std::lock_guard lock(mutex_);
    GrowTo(offset);
    return cursor_ = offset;
  }

  Result<uint64_t> SeekCur(int64_t offset) override {
    std::lock_guard lock(mutex_);
    uint64_t new_pos =
        std::max<int64_t>(static_cast<int64_t>(cursor_) + offset, 0);
    GrowTo(new_pos);
    return cursor_ = new_pos;
  }

  Result<uint64_t> SeekEnd(int64_t offset) override {
    std::lock_guard lock(mutex_);
    uint64_t new_pos =
        std::max<int64_t>(static_cast<int64_t>(data_.size()) + offset, 0);
    GrowTo(new_pos);
    return cursor_ = new_pos;
  }

  Result<uint64_t> PRead(void* buf, uint64_t count,
                         uint64_t offset) const override {
    std::shared_lock lock(mutex_);
    uint64_t to_read = ClampRange(offset, count);
    memcpy(buf, &data_[offset], to_read);
    return to_read;
  }

  Result<uint64_t> PWrite(const void* buf, uint64_t count,
                          uint64_t offset) override {
    std::lock_guard lock(mutex_);
    GrowTo(offset + count);
    memcpy(&data_[offset], buf, count);
    return count;
  }

 private:
  // Must be called with the lock held for reading or writing
  uint64_t ClampRange(uint64_t begin, uint64_t length) const {
    if (begin > data_.size()) {
      return 0;
    }
    return std::min(length, data_.size() - begin);
  }

  // Must be called with the lock held for writing
  void GrowTo(uint64_t new_size) {
    if (data_.size() < new_size) {
      data_.resize(new_size, '\0');
    }
  }

  std::vector<char>& data_;
  uint64_t cursor_ = 0;
  std::shared_mutex& mutex_;
};

class OwningInMemoryIo : public InMemoryIo {
 public:
  OwningInMemoryIo() : InMemoryIo(data_, mutex_) {}
  OwningInMemoryIo(std::vector<char> data)
      : InMemoryIo(data_, mutex_), data_(std::move(data)) {}

 private:
  std::vector<char> data_;
  std::shared_mutex mutex_;
};

class InMemoryFilesystemImpl : public ReadWriteFilesystem {
 public:
  Result<std::unique_ptr<ReaderSeeker>> OpenReadOnly(
      std::string_view path) override {
    return CF_EXPECT(OpenReadWrite(path));
  }

  Result<std::unique_ptr<ReaderWriterSeeker>> CreateFile(
      std::string_view path) override {
    std::lock_guard lock(mutex_);
    auto [it, inserted] =
        files_.try_emplace(std::string(path), std::vector<char>{});
    CF_EXPECTF(!!inserted, "'{}' already exists", path);
    return std::make_unique<InMemoryIo>(it->second, mutex_);
  }

  Result<void> DeleteFile(std::string_view path) override {
    std::lock_guard lock(mutex_);
    auto it = files_.find(path);
    CF_EXPECTF(it != files_.end(), "No such file '{}'", path);
    files_.erase(it);
    return {};
  }

  Result<std::unique_ptr<ReaderWriterSeeker>> OpenReadWrite(
      std::string_view path) override {
    std::lock_guard lock(mutex_);
    auto it = files_.find(path);
    CF_EXPECTF(it != files_.end(), "'{}' does not exist", path);
    return std::make_unique<InMemoryIo>(it->second, mutex_);
  }

 private:
  // TODO: schuffelen - add per-file mutexes
  std::map<std::string, std::vector<char>, std::less<void>> files_;
  std::shared_mutex mutex_;
};

}  // namespace

std::unique_ptr<ReaderWriterSeeker> InMemoryIo() {
  return std::make_unique<OwningInMemoryIo>();
}

std::unique_ptr<ReaderWriterSeeker> InMemoryIo(std::vector<char> data) {
  return std::make_unique<OwningInMemoryIo>(std::move(data));
}

std::unique_ptr<ReadWriteFilesystem> InMemoryFilesystem() {
  return std::make_unique<InMemoryFilesystemImpl>();
}

}  // namespace cuttlefish

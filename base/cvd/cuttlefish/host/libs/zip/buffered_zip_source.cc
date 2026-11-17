//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/libs/zip/buffered_zip_source.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/length.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class BufferedZipSourceCallbacks : public ReaderFakeSeeker {
 public:
  static Result<std::unique_ptr<BufferedZipSourceCallbacks>> Create(
      std::unique_ptr<ReaderSeeker> data_provider, uint64_t buffer_size) {
    CF_EXPECT(data_provider.get());

    uint64_t backing_size = CF_EXPECT(Length(*data_provider));
    buffer_size = std::min(buffer_size, backing_size);

    std::unique_ptr<BufferedZipSourceCallbacks> callbacks(
        new BufferedZipSourceCallbacks(buffer_size, backing_size));

    callbacks->data_provider_ = std::move(data_provider);

    return callbacks;
  }

  Result<void> Visit(IoVisitor& visitor) override {
    CF_EXPECT(visitor.Accept(*this));
    return {};
  }

  Result<uint64_t, int> PRead(void* data, uint64_t len,
                              uint64_t offset) const override {
    BufferedZipSourceCallbacks* mut =
        const_cast<BufferedZipSourceCallbacks*>(this);
    return CF_EXPECT(mut->PReadMut(data, len, offset));
  }

 private:
  explicit BufferedZipSourceCallbacks(uint64_t buffer_size,
                                      uint64_t backing_size)
      : ReaderFakeSeeker(backing_size),
        buffer_(buffer_size),
        backing_size_(backing_size) {}

  Result<uint64_t, int> PReadMut(void* data, uint64_t len, uint64_t offset) {
    if (offset >= backing_size_ || len == 0) {
      return 0;
    }
    len = std::min(len, backing_size_ - offset);
    if (len > buffer_.size()) {
      return CF_EXPECT(data_provider_->PRead(data, len, offset));
    }
    {  // If data can be read from the buffer, can share read access
      std::shared_lock lock(mutex_);
      if (uint64_t buf_read = ReadBufLocked(data, len, offset); buf_read > 0) {
        return buf_read;
      }
    }
    {  // If we need to load new data, get exclusive access to fill the buffer
      std::lock_guard lock(mutex_);
      // Another thread may have loaded data we want between the unlock / lock
      if (uint64_t buf_rd = ReadBufLocked(data, len, offset); buf_rd > 0) {
        return buf_rd;
      }  // We need new data.
      buf_begin_ = std::min(backing_size_ - buffer_.size(), offset);
      has_data_ = false;  // If PReadExact fails, assume buffer_ is corrupted.
      CF_EXPECT(PReadExact(*data_provider_, buffer_.data(), buffer_.size(),
                           buf_begin_));
      has_data_ = true;
      return ReadBufLocked(data, len, offset);
    }
  }

  uint64_t ReadBufLocked(void* data, uint64_t len, uint64_t off) {
    if (!has_data_ || off < buf_begin_ || off >= buf_begin_ + buffer_.size()) {
      return 0;
    }
    uint64_t buf_offset = off - buf_begin_;
    uint64_t buf_read_len = std::min(len, buffer_.size() - buf_offset);
    memcpy(data, &buffer_[buf_offset], buf_read_len);
    return buf_read_len;
  }

  std::unique_ptr<ReaderSeeker> data_provider_;
  std::shared_mutex mutex_;
  bool has_data_ = false;
  std::vector<char> buffer_;
  uint64_t backing_size_ = 0;
  uint64_t buf_begin_ = 0;
};

}  // namespace

Result<SeekableZipSource> BufferZipSource(
    std::unique_ptr<ReaderSeeker> data_provider, uint64_t buffer_size) {
  std::unique_ptr<BufferedZipSourceCallbacks> callbacks =
      CF_EXPECT(BufferedZipSourceCallbacks::Create(std::move(data_provider),
                                                   buffer_size));

  return CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks)));
}

Result<ReadableZip> BufferAndOpenZip(std::unique_ptr<ReaderSeeker> source) {
  SeekableZipSource buffered =
      CF_EXPECT(BufferZipSource(std::move(source), 1 << 26));

  return CF_EXPECT(ReadableZip::FromSource(std::move(buffered)));
}

}  // namespace cuttlefish

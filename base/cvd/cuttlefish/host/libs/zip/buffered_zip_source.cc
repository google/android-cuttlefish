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
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_callback.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/length.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class BufferedZipSourceCallbacks : public SeekableZipSourceCallback {
 public:
  static Result<std::unique_ptr<BufferedZipSourceCallbacks>> Create(
      std::unique_ptr<ReaderSeeker> data_provider, size_t buffer_size) {
    CF_EXPECT(data_provider.get());
    std::unique_ptr<BufferedZipSourceCallbacks> callbacks(
        new BufferedZipSourceCallbacks(buffer_size));

    callbacks->size_ = CF_EXPECT(Length(*data_provider));
    callbacks->data_provider_ = std::move(data_provider);

    return callbacks;
  }

  static Result<std::unique_ptr<BufferedZipSourceCallbacks>> Create(
      SeekableZipSource source, size_t buffer_size) {
    std::unique_ptr<BufferedZipSourceCallbacks> callbacks(
        new BufferedZipSourceCallbacks(buffer_size));

    callbacks->source_ = std::move(source);
    callbacks->data_provider_ = std::make_unique<SeekingZipSourceReader>(
        CF_EXPECT(callbacks->source_->Reader()));
    callbacks->size_ = CF_EXPECT(Length(*callbacks->data_provider_));

    return callbacks;
  }

  bool Close() override {
    offset_ = 0;
    buffer_remaining_ = 0;
    offset_in_buffer_ = 0;
    buffer_remaining_ = 0;

    return true;
  }
  bool Open() override {
    offset_ = 0;
    buffer_remaining_ = 0;
    offset_in_buffer_ = 0;
    buffer_remaining_ = 0;

    return data_provider_->SeekSet(0).ok();
  }
  int64_t Read(char* data, uint64_t len) override {
    if (len > buffer_.size()) {
      buffer_remaining_ = 0;
      if (!data_provider_->SeekSet(offset_).ok()) {
        return false;
      }
      VLOG(1) << "Bypassing buffer, reading " << len;
      if (Result<uint64_t> read_len = data_provider_->Read(data, len);
          read_len.ok()) {
        offset_ += *read_len;
        return *read_len;
      } else {
        return -1;
      }
    }
    if (buffer_remaining_ > 0) {
      uint64_t to_read = std::min(len, buffer_remaining_);
      memcpy(data, &buffer_[offset_in_buffer_], to_read);
      buffer_remaining_ -= to_read;
      offset_in_buffer_ += to_read;
      offset_ += to_read;
      return to_read;
    }
    uint64_t buffer_fill = buffer_.size();
    if (offset_ + buffer_fill > size_) {
      buffer_fill = size_ - offset_;
    }
    if (buffer_fill == 0) {
      return 0;
    }
    if (!data_provider_->SeekSet(offset_).ok()) {
      return -1;
    }
    VLOG(1) << "Filling buffer with " << buffer_fill;
    Result<size_t> data_provider_read =
        data_provider_->Read(buffer_.data(), buffer_fill);
    if (!data_provider_read.ok()) {
      return -1;
    }
    buffer_remaining_ = *data_provider_read;
    offset_in_buffer_ = 0;
    return Read(data, len);
  }
  uint64_t Size() override { return size_; }
  bool SetOffset(int64_t new_offset) override {
    if (new_offset >= offset_ && new_offset < offset_ + buffer_remaining_) {
      offset_in_buffer_ += new_offset - offset_;
      buffer_remaining_ -= new_offset - offset_;
    } else {
      buffer_remaining_ = 0;
    }
    offset_ = new_offset;
    return true;
  }
  int64_t Offset() override { return offset_; }

 private:
  explicit BufferedZipSourceCallbacks(size_t buffer_size)
      : buffer_(buffer_size) {}

  std::unique_ptr<ReaderSeeker> data_provider_;
  std::optional<SeekableZipSource> source_;
  std::vector<char> buffer_;
  uint64_t size_ = 0;
  int64_t offset_ = 0;
  size_t offset_in_buffer_ = 0;
  size_t buffer_remaining_ = 0;
};

}  // namespace

Result<SeekableZipSource> BufferZipSource(
    std::unique_ptr<ReaderSeeker> data_provider, size_t buffer_size) {
  std::unique_ptr<BufferedZipSourceCallbacks> callbacks =
      CF_EXPECT(BufferedZipSourceCallbacks::Create(std::move(data_provider),
                                                   buffer_size));

  return CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks)));
}

Result<SeekableZipSource> BufferZipSource(SeekableZipSource source,
                                          size_t buffer_size) {
  std::unique_ptr<BufferedZipSourceCallbacks> callbacks = CF_EXPECT(
      BufferedZipSourceCallbacks::Create(std::move(source), buffer_size));

  return CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks)));
}

}  // namespace cuttlefish

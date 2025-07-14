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

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {
namespace {

class BufferedZipSourceCallbacks : public SeekableZipSourceCallback {
 public:
  static Result<BufferedZipSourceCallbacks> Create(SeekableZipSource inner,
                                                   size_t buffer_size) {
    BufferedZipSourceCallbacks callbacks(std::move(inner), buffer_size);

    ZipStat zip_stat = CF_EXPECT(callbacks.inner_.Stat());
    callbacks.size_ = CF_EXPECT(std::move(zip_stat.size));

    return callbacks;
  }

  bool Close() override {
    offset_ = 0;
    buffer_remaining_ = 0;
    reader_ = std::nullopt;
    return true;
  }
  bool Open() override {
    offset_ = 0;
    buffer_remaining_ = 0;

    Result<SeekingZipSourceReader> reader = inner_.Reader();
    if (reader.ok()) {
      reader_ = std::move(*reader);
      return true;
    } else {
      reader_ = std::nullopt;
      return false;
    }
  }
  int64_t Read(char* data, uint64_t len) override {
    if (!reader_) {
      return -1;
    }
    if (len > buffer_.size()) {
      buffer_remaining_ = 0;
      if (!reader_->SeekFromStart(offset_).ok()) {
        return false;
      }
      Result<uint64_t> data_read = reader_->Read(data, len);
      if (data_read.ok()) {
        offset_ += *data_read;
        return *data_read;
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
    if (!reader_->SeekFromStart(offset_).ok()) {
      return -1;
    }
    Result<size_t> inner_read = reader_->Read(buffer_.data(), buffer_fill);
    if (!inner_read.ok()) {
      return -1;
    }
    buffer_remaining_ = *inner_read;
    offset_in_buffer_ = 0;
    return Read(data, len);
  }
  uint64_t Size() override { return size_; }
  bool SetOffset(int64_t offset) override {
    offset_ = offset;
    buffer_remaining_ = 0;
    return true;
  }
  int64_t Offset() override { return offset_; }

 private:
  BufferedZipSourceCallbacks(SeekableZipSource inner, size_t buffer_size)
      : inner_(std::move(inner)), buffer_(buffer_size) {}

  SeekableZipSource inner_;
  std::optional<SeekingZipSourceReader> reader_;
  std::vector<char> buffer_;
  uint64_t size_;
  int64_t offset_;
  size_t offset_in_buffer_;
  size_t buffer_remaining_;
};

}  // namespace

Result<SeekableZipSource> BufferZipSource(SeekableZipSource inner,
                                          size_t buffer_size) {
  BufferedZipSourceCallbacks callbacks = CF_EXPECT(
      BufferedZipSourceCallbacks::Create(std::move(inner), buffer_size));
  std::unique_ptr<SeekableZipSourceCallback> callbacks_ptr =
      std::make_unique<BufferedZipSourceCallbacks>(std::move(callbacks));
  return CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks_ptr)));
}

}  // namespace cuttlefish

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

#include "cuttlefish/host/libs/zip/cached_zip_source.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"

#include "cuttlefish/host/libs/zip/lazily_loaded_file.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_callback.h"
#include "cuttlefish/host/libs/zip/libzip_cc/stat.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class CachedZipSourceCallbacks : public SeekableZipSourceCallback {
 public:
  CachedZipSourceCallbacks(LazilyLoadedFile source, size_t size)
      : source_(std::move(source)), size_(size) {}

  bool Close() override {
    offset_ = 0;
    return true;
  }

  bool Open() override {
    offset_ = 0;
    return true;
  }

  int64_t Read(char* data, uint64_t len) override {
    VLOG(1) << "Reading " << len;
    if (Result<void> res = source_.Seek(offset_); !res.ok()) {
      LOG(ERROR) << res.error();
      return -1;
    }
    Result<size_t> res = source_.Read(data, len);
    if (res.ok()) {
      offset_ += *res;
      return *res;
    } else {
      LOG(ERROR) << res.error();
      return -1;
    }
  }

  uint64_t Size() override { return size_; }

  bool SetOffset(int64_t offset) override {
    VLOG(1) << "Setting offset to " << offset;
    offset_ = offset;
    return true;
  }

  int64_t Offset() override { return offset_; }

 private:
  LazilyLoadedFile source_;
  size_t offset_ = 0;
  const size_t size_;
};

}  // namespace

Result<SeekableZipSource> CacheZipSource(SeekableZipSource inner,
                                         std::string file_path) {
  ZipStat zip_stat = CF_EXPECT(inner.Stat());
  size_t size = CF_EXPECT(std::move(zip_stat.size));

  std::unique_ptr<ReaderSeeker> reader =
      CF_EXPECT(ZipSourceAsReaderSeeker(std::move(inner)));

  LazilyLoadedFile file = CF_EXPECT(
      LazilyLoadedFile::Create(std::move(file_path), size, std::move(reader)));

  CachedZipSourceCallbacks callbacks(std::move(file), size);

  std::unique_ptr<SeekableZipSourceCallback> callbacks_ptr =
      std::make_unique<CachedZipSourceCallbacks>(std::move(callbacks));

  return CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks_ptr)));
}

}  // namespace cuttlefish

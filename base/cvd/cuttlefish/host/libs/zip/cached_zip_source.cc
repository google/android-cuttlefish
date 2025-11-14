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

#include <android-base/logging.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/lazily_loaded_file.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_callback.h"
#include "cuttlefish/host/libs/zip/libzip_cc/stat.h"

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
    LOG(VERBOSE) << "Reading " << len;
    if (Result<void> res = source_.Seek(offset_); !res.ok()) {
      LOG(ERROR) << res.error().FormatForEnv();
      return -1;
    }
    Result<size_t> res = source_.Read(data, len);
    if (res.ok()) {
      offset_ += *res;
      return *res;
    } else {
      LOG(ERROR) << res.error().FormatForEnv();
      return -1;
    }
  }

  uint64_t Size() override { return size_; }

  bool SetOffset(int64_t offset) override {
    LOG(VERBOSE) << "Setting offset to " << offset;
    offset_ = offset;
    return true;
  }

  int64_t Offset() override { return offset_; }

 private:
  LazilyLoadedFile source_;
  size_t offset_ = 0;
  const size_t size_;
};

class LazilyLoadedZipSourceFile : public LazilyLoadedFileReadCallback {
 public:
  LazilyLoadedZipSourceFile(std::unique_ptr<SeekableZipSource> source,
                            SeekingZipSourceReader reader)
      : source_(std::move(source)), reader_(std::move(reader)) {}

  Result<size_t> Seek(size_t offset) override {
    CF_EXPECT(reader_.SeekFromStart(offset));
    return offset;
  }
  Result<size_t> Read(char* data, size_t size) override {
    return CF_EXPECT(reader_.Read(data, size));
  }

 private:
  std::unique_ptr<SeekableZipSource> source_;
  SeekingZipSourceReader reader_;
};

}  // namespace

Result<SeekableZipSource> CacheZipSource(SeekableZipSource inner,
                                         std::string file_path) {
  ZipStat zip_stat = CF_EXPECT(inner.Stat());
  size_t size = CF_EXPECT(std::move(zip_stat.size));

  std::unique_ptr<SeekableZipSource> unique_inner =
      std::make_unique<SeekableZipSource>(std::move(inner));
  CF_EXPECT(unique_inner.get());

  SeekingZipSourceReader reader = CF_EXPECT(unique_inner->Reader());

  std::unique_ptr<LazilyLoadedFileReadCallback> file_callbacks =
      std::make_unique<LazilyLoadedZipSourceFile>(std::move(unique_inner),
                                                  std::move(reader));

  LazilyLoadedFile file = CF_EXPECT(LazilyLoadedFile::Create(
      std::move(file_path), size, std::move(file_callbacks)));

  CachedZipSourceCallbacks callbacks(std::move(file), size);

  std::unique_ptr<SeekableZipSourceCallback> callbacks_ptr =
      std::make_unique<CachedZipSourceCallbacks>(std::move(callbacks));

  return CF_EXPECT(SeekableZipSource::FromCallbacks(std::move(callbacks_ptr)));
}

}  // namespace cuttlefish

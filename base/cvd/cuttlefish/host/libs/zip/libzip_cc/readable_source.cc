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

#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "zip.h"

#include "cuttlefish/host/libs/zip/libzip_cc/error.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/stat.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

std::optional<ZipCompression> CompressionFromRaw(uint16_t method) {
  switch (method) {
    case ZIP_CM_DEFAULT:
      return ZipCompression::kDefault;
    case ZIP_CM_STORE:
      return ZipCompression::kStore;
    case ZIP_CM_BZIP2:
      return ZipCompression::kBzip2;
    case ZIP_CM_DEFLATE:
      return ZipCompression::kDeflate;
    case ZIP_CM_XZ:
      return ZipCompression::kXz;
    case ZIP_CM_ZSTD:
      return ZipCompression::kZstd;
    default:
      return {};
  }
}

}  // namespace

ReadableZipSource::ReadableZipSource(ManagedZipSource raw)
    : raw_(std::move(raw)) {}
// These have to be defined in the `.cc` file to avoid linker errors because of
// bazel weirdness around cmake files.
ReadableZipSource::ReadableZipSource(ReadableZipSource&&) = default;
ReadableZipSource::~ReadableZipSource() = default;
ReadableZipSource& ReadableZipSource::operator=(ReadableZipSource&&) = default;

Result<ZipStat> ReadableZipSource::Stat() {
  zip_source_t* raw_source = CF_EXPECT(raw_.get());

  zip_stat_t raw_stat;
  zip_stat_init(&raw_stat);

  CF_EXPECT_EQ(zip_source_stat(raw_source, &raw_stat), 0,
               ZipErrorString(raw_source));

  ZipStat ret;
  if (raw_stat.valid & ZIP_STAT_NAME) {
    ret.name = std::string(raw_stat.name);
  }
  if (raw_stat.valid & ZIP_STAT_INDEX) {
    ret.index = raw_stat.index;
  }
  if (raw_stat.valid & ZIP_STAT_SIZE) {
    ret.size = raw_stat.size;
  }
  if (raw_stat.valid & ZIP_STAT_COMP_SIZE) {
    ret.compressed_size = raw_stat.comp_size;
  }
  if (raw_stat.valid & ZIP_STAT_COMP_METHOD) {
    ret.compression_method = CompressionFromRaw(raw_stat.comp_method);
  }
  return ret;
}

Result<ZipSourceReader> ZipSourceReader::Open(ReadableZipSource& source) {
  zip_source_t* raw_source = CF_EXPECT(source.raw_.get());

  CF_EXPECT_EQ(zip_source_open(raw_source), 0, ZipErrorString(raw_source));

  return ZipSourceReader(&source);
}

ZipSourceReader::ZipSourceReader(ReadableZipSource* source) : source_(source) {}

ZipSourceReader::ZipSourceReader(ZipSourceReader&& other)
    : source_(other.source_) {
  other.source_ = nullptr;
}

ZipSourceReader& ZipSourceReader::operator=(ZipSourceReader&& other) {
  source_ = other.source_;
  other.source_ = nullptr;
  return *this;
}

ZipSourceReader::~ZipSourceReader() {
  if (source_ && source_->raw_) {
    zip_source_close(source_->raw_.get());
  }
}

Result<uint64_t, int> ZipSourceReader::Read(void* data, uint64_t length) {
  std::lock_guard lock(mutex_);

  CF_EXPECTV(source_ != nullptr, EBADF, "Empty internal source");
  zip_source_t* raw_source =
      CF_EXPECTV(source_->raw_.get(), EBADF, "Empty internal source");

  int64_t read_res = zip_source_read(raw_source, data, length);

  CF_EXPECTVF(read_res >= 0, ZipErrno(raw_source), "Read failed: '{}'",
              ZipErrorString(raw_source));

  return read_res;
}

Result<void> ZipSourceReader::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

}  // namespace cuttlefish

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

#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"

#include <stdint.h>
#include <stdio.h>

#include <string>
#include <utility>

#include "zip.h"

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/libzip_cc/error.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"

namespace cuttlefish {

Result<WritableZipSource> WritableZipSource::BorrowData(const void* data,
                                                        size_t size) {
  CF_EXPECT_NE(data, nullptr);

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_buffer_create(data, size, 0, error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return WritableZipSource(std::move(source));
}

Result<WritableZipSource> WritableZipSource::FromFile(const std::string& path) {
  ManagedZipError error = NewZipError();
  ManagedZipSource source(
      zip_source_file_create(path.c_str(), 0, ZIP_LENGTH_TO_END, error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return WritableZipSource(std::move(source));
}

WritableZipSource::WritableZipSource(ManagedZipSource raw)
    : SeekableZipSource(std::move(raw)) {}

Result<ZipSourceWriter> WritableZipSource::Writer() {
  zip_source_t* raw = CF_EXPECT(raw_.get());

  CF_EXPECT_EQ(zip_source_begin_write(raw), 0, ZipErrorString(raw));

  return ZipSourceWriter(this);
}

ZipSourceWriter::ZipSourceWriter(WritableZipSource* source) : source_(source) {}

ZipSourceWriter::ZipSourceWriter(ZipSourceWriter&& other)
    : source_(other.source_) {
  other.source_ = nullptr;
}

ZipSourceWriter& ZipSourceWriter::operator=(ZipSourceWriter&& other) {
  source_ = other.source_;
  other.source_ = nullptr;
  return *this;
}

ZipSourceWriter::~ZipSourceWriter() {
  if (source_ && source_->raw_) {
    zip_source_rollback_write(source_->raw_.get());
  }
}

Result<uint64_t> ZipSourceWriter::Write(void* data, uint64_t length) {
  CF_EXPECT_NE(data, nullptr);
  CF_EXPECT_NE(source_, nullptr);
  zip_source_t* raw_source = CF_EXPECT(source_->raw_.get());

  int64_t written = zip_source_write(raw_source, data, length);
  CF_EXPECT_GE(written, 0, ZipErrorString(raw_source));
  return static_cast<uint64_t>(written);
}

Result<void> ZipSourceWriter::SeekFromStart(int64_t offset) {
  CF_EXPECT_NE(source_, nullptr);
  zip_source_t* raw_source = CF_EXPECT(source_->raw_.get());

  CF_EXPECT_EQ(zip_source_seek_write(raw_source, offset, SEEK_SET), 0,
               ZipErrorString(raw_source));

  return {};
}

Result<void> ZipSourceWriter::Finalize(ZipSourceWriter writer) {
  CF_EXPECT_NE(writer.source_, nullptr);
  zip_source_t* raw = CF_EXPECT(writer.source_->raw_.get());

  CF_EXPECT_EQ(zip_source_commit_write(raw), 0, ZipErrorString(raw));

  return {};
}

}  // namespace cuttlefish

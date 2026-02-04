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

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <mutex>
#include <utility>

#include "zip.h"

#include "cuttlefish/io/fake_pread_pwrite.h"
#include "cuttlefish/host/libs/zip/libzip_cc/error.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_callback.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

struct SeekableCallbackSource {
  SeekableCallbackSource(std::unique_ptr<SeekableZipSourceCallback> callbacks)
      : callbacks_(std::move(callbacks)), error_(NewZipError()) {}

  std::unique_ptr<SeekableZipSourceCallback> callbacks_;
  ManagedZipError error_;
};

int64_t HandleCallback(SeekableZipSourceCallback& callbacks, zip_error_t* error,
                       void* data, uint64_t len, zip_source_cmd_t cmd) {
  switch (cmd) {
    case ZIP_SOURCE_SEEK: {
      int64_t new_offset = zip_source_seek_compute_offset(
          callbacks.Offset(), callbacks.Size(), data, len, error);
      if (!callbacks.SetOffset(new_offset)) {
        zip_error_set(error, ZIP_ER_SEEK, errno);
        return -1;
      }
      return 0;
    }
    case ZIP_SOURCE_SUPPORTS:
      return zip_source_make_command_bitmap(
          ZIP_SOURCE_CLOSE, ZIP_SOURCE_ERROR, ZIP_SOURCE_FREE, ZIP_SOURCE_OPEN,
          ZIP_SOURCE_READ, ZIP_SOURCE_SEEK, ZIP_SOURCE_STAT,
          ZIP_SOURCE_SUPPORTS, ZIP_SOURCE_TELL, -1);
    case ZIP_SOURCE_TELL:
      return callbacks.Offset();
    default:
      return HandleCallback(static_cast<ReadableZipSourceCallback&>(callbacks),
                            error, data, len, cmd);
  }
}

// https://libzip.org/documentation/zip_source_function.html
int64_t SeekableZipSourceCallbackFn(void* userdata, void* data, uint64_t len,
                                    zip_source_cmd_t cmd) {
  SeekableCallbackSource* source =
      reinterpret_cast<SeekableCallbackSource*>(userdata);
  if (cmd == ZIP_SOURCE_FREE) {
    delete source;
    return 0;
  }
  return HandleCallback(*source->callbacks_, source->error_.get(), data, len,
                        cmd);
}

}  // namespace

Result<SeekableZipSource> SeekableZipSource::FromCallbacks(
    std::unique_ptr<SeekableZipSourceCallback> callbacks) {
  CF_EXPECT(callbacks.get());

  std::unique_ptr<SeekableCallbackSource> wrapped_source =
      std::make_unique<SeekableCallbackSource>(std::move(callbacks));

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_function_create(
      SeekableZipSourceCallbackFn, wrapped_source.release(), error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return SeekableZipSource(std::move(source));
}

Result<SeekingZipSourceReader> SeekableZipSource::Reader() {
  zip_source_t* raw_source = CF_EXPECT(raw_.get());

  CF_EXPECT_EQ(zip_source_open(raw_source), 0, ZipErrorString(raw_source));

  return SeekingZipSourceReader(this);
}

SeekableZipSource::SeekableZipSource(ManagedZipSource raw)
    : ReadableZipSource(std::move(raw)) {}

SeekingZipSourceReader::SeekingZipSourceReader(SeekingZipSourceReader&&) =
    default;
SeekingZipSourceReader::~SeekingZipSourceReader() = default;
SeekingZipSourceReader& SeekingZipSourceReader::operator=(
    SeekingZipSourceReader&&) = default;

Result<uint64_t> SeekingZipSourceReader::SeekSet(uint64_t offset) {
  return CF_EXPECT(Seek(offset, SEEK_SET));
}

Result<uint64_t> SeekingZipSourceReader::SeekCur(int64_t offset) {
  return CF_EXPECT(Seek(offset, SEEK_CUR));
}

Result<uint64_t> SeekingZipSourceReader::SeekEnd(int64_t offset) {
  return CF_EXPECT(Seek(offset, SEEK_END));
}

Result<uint64_t> SeekingZipSourceReader::Read(void* data, uint64_t length) {
  return CF_EXPECT(ZipSourceReader::Read(data, length));
}

SeekingZipSourceReader::SeekingZipSourceReader(SeekableZipSource* ptr)
    : ZipSourceReader(ptr) {}

Result<uint64_t> SeekingZipSourceReader::Seek(int64_t offset, int whence) {
  std::lock_guard lock(mutex_);
  CF_EXPECT_NE(source_, nullptr);
  zip_source_t* raw_source = CF_EXPECT(source_->raw_.get());

  CF_EXPECT_EQ(zip_source_seek(raw_source, offset, whence), 0,
               ZipErrorString(raw_source));

  int64_t tell = zip_source_tell(raw_source);
  CF_EXPECT_GE(tell, 0, ZipErrorString(raw_source));

  return tell;
}

Result<uint64_t> SeekingZipSourceReader::PRead(void* buf, uint64_t count,
                                               uint64_t offset) const {
  auto& non_const = const_cast<SeekingZipSourceReader&>(*this);
  std::lock_guard lock(non_const.mutex_);
  return CF_EXPECT(FakePRead(non_const, buf, count, offset));
}

}  // namespace cuttlefish

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

#include <map>
#include <memory>
#include <mutex>
#include <utility>

#include "absl/base/no_destructor.h"
#include "zip.h"

#include "cuttlefish/host/libs/zip/libzip_cc/error.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/io/fake_pread_pwrite.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/length.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

struct CallbackUserdata {
  CallbackUserdata(std::unique_ptr<ReaderSeeker> callbacks)
      : callbacks_(std::move(callbacks)), error_(NewZipError()) {}

  std::unique_ptr<ReaderSeeker> callbacks_;
  ManagedZipError error_;
};

Result<int64_t, int> HandleCallback(ReaderSeeker& callbacks, zip_error_t* error,
                                    void* data, uint64_t len,
                                    zip_source_cmd_t cmd) {
  switch (cmd) {
    case ZIP_SOURCE_CLOSE:
    case ZIP_SOURCE_OPEN:
      CF_EXPECT(callbacks.SeekSet(0));
      return 0;
    case ZIP_SOURCE_READ:
      return CF_EXPECT(callbacks.Read(reinterpret_cast<char*>(data), len));
    case ZIP_SOURCE_STAT: {
      zip_stat_t* stat_out = ZIP_SOURCE_GET_ARGS(zip_stat_t, data, len, error);
      CF_EXPECTV(stat_out != nullptr, EINVAL, "invalid zip_stat_t");
      zip_stat_init(stat_out);
      stat_out->valid = ZIP_STAT_SIZE;
      stat_out->size = CF_EXPECT(Length(callbacks));
      return sizeof(zip_stat_t);
    }
    case ZIP_SOURCE_SEEK: {
      int64_t current_offset = CF_EXPECT(callbacks.SeekCur(0));
      uint64_t size = CF_EXPECT(Length(callbacks));
      int64_t new_offset = zip_source_seek_compute_offset(current_offset, size,
                                                          data, len, error);
      CF_EXPECTV(new_offset >= 0, EINVAL, "invalid offset");
      CF_EXPECT(callbacks.SeekSet(new_offset));
      return 0;
    }
    case ZIP_SOURCE_SUPPORTS:
      return zip_source_make_command_bitmap(
          ZIP_SOURCE_CLOSE, ZIP_SOURCE_ERROR, ZIP_SOURCE_FREE, ZIP_SOURCE_OPEN,
          ZIP_SOURCE_READ, ZIP_SOURCE_SEEK, ZIP_SOURCE_STAT,
          ZIP_SOURCE_SUPPORTS, ZIP_SOURCE_TELL, -1);
    case ZIP_SOURCE_TELL:
      return CF_EXPECT(callbacks.SeekCur(0));
    default:
      return CF_ERRVF(EOPNOTSUPP, "Unknown operation {}", int(cmd));
  }
}

// https://libzip.org/documentation/zip_source_function.html
int64_t SeekableZipSourceCallbackFn(void* userdata, void* data, uint64_t len,
                                    zip_source_cmd_t cmd) {
  CallbackUserdata* source = reinterpret_cast<CallbackUserdata*>(userdata);
  if (cmd == ZIP_SOURCE_FREE) {
    delete source;
    return 0;
  } else if (cmd == ZIP_SOURCE_ERROR) {
    return zip_error_to_data(source->error_.get(), data, len);
  }
  zip_error_set(source->error_.get(), ZIP_ER_OK, 0);
  // TODO(schuffelen): Hang on to this value to propagate to wrapper functions
  Result<int64_t, int> res =
      HandleCallback(*source->callbacks_, source->error_.get(), data, len, cmd);
  if (res.has_value()) {
    return res.value();
  } else if (res.error().Value() == EOPNOTSUPP) {
    zip_error_set(source->error_.get(), ZIP_ER_OPNOTSUPP, EOPNOTSUPP);
    return -1;
  }
  static absl::NoDestructor<std::map<zip_source_cmd_t, int>> kCodeMap({
      {ZIP_SOURCE_CLOSE, ZIP_ER_CLOSE},
      {ZIP_SOURCE_OPEN, ZIP_ER_OPEN},
      {ZIP_SOURCE_READ, ZIP_ER_READ},
      {ZIP_SOURCE_SEEK, ZIP_ER_SEEK},
      {ZIP_SOURCE_TELL, ZIP_ER_TELL},
  });
  auto err_it = kCodeMap->find(cmd);
  int zip_err_code = err_it == kCodeMap->end() ? ZIP_ER_INVAL : err_it->second;
  zip_error_set(source->error_.get(), zip_err_code, *res.error());
  return -1;
}

}  // namespace

Result<SeekableZipSource> SeekableZipSource::FromCallbacks(
    std::unique_ptr<ReaderSeeker> callbacks) {
  CF_EXPECT(callbacks.get());

  std::unique_ptr<CallbackUserdata> userdata =
      std::make_unique<CallbackUserdata>(std::move(callbacks));

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_function_create(
      SeekableZipSourceCallbackFn, userdata.get(), error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  userdata.release();  // Owned by source if zip_source_function_create succeeds
  // Even though `release` is here, the `delete` path is with ZIP_SOURCE_FREE.
  return SeekableZipSource(std::move(source));
}

Result<SeekingZipSourceReader> SeekingZipSourceReader::Open(
    SeekableZipSource& reader) {
  zip_source_t* raw_source = CF_EXPECT(reader.raw_.get());

  CF_EXPECT_EQ(zip_source_open(raw_source), 0, ZipErrorString(raw_source));

  return SeekingZipSourceReader(&reader);
}

SeekableZipSource::SeekableZipSource(ManagedZipSource raw)
    : ReadableZipSource(std::move(raw)) {}

Result<void> SeekingZipSourceReader::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(static_cast<ReaderSeeker&>(*this)));
  return {};
}

Result<uint64_t, int> SeekingZipSourceReader::SeekSet(uint64_t offset) {
  return CF_EXPECT(Seek(offset, SEEK_SET));
}

Result<uint64_t, int> SeekingZipSourceReader::SeekCur(int64_t offset) {
  return CF_EXPECT(Seek(offset, SEEK_CUR));
}

Result<uint64_t, int> SeekingZipSourceReader::SeekEnd(int64_t offset) {
  return CF_EXPECT(Seek(offset, SEEK_END));
}

Result<uint64_t, int> SeekingZipSourceReader::Read(void* data,
                                                   uint64_t length) {
  return CF_EXPECT(ZipSourceReader::Read(data, length));
}

SeekingZipSourceReader::SeekingZipSourceReader(SeekableZipSource* ptr)
    : ZipSourceReader(ptr) {}

Result<uint64_t, int> SeekingZipSourceReader::Seek(int64_t offset, int whence) {
  std::lock_guard lock(mutex_);
  CF_EXPECTVF(source_ != nullptr, EBADF, "Seek({}, {}) failed", offset, whence);
  zip_source_t* raw_source = CF_EXPECTVF(source_->raw_.get(), EBADF,
                                         "Seek({}, {}) failed", offset, whence);

  if (zip_source_seek(raw_source, offset, whence) != 0) {
    return CF_ERRVF(ZipErrno(raw_source),
                    "zip_source_seek(..., {}, {}) failed: {}", offset, whence,
                    ZipErrorString(raw_source));
  }

  int64_t tell = zip_source_tell(raw_source);
  if (tell < 0) {
    return CF_ERRVF(ZipErrno(raw_source), "zip_source_tell(...) failed: {}",
                    ZipErrorString(raw_source));
  }

  return tell;
}

Result<uint64_t, int> SeekingZipSourceReader::PRead(void* buf, uint64_t count,
                                                    uint64_t offset) const {
  auto& non_const = const_cast<SeekingZipSourceReader&>(*this);
  std::lock_guard lock(non_const.mutex_);
  return CF_EXPECT(FakePRead(non_const, buf, count, offset));
}

}  // namespace cuttlefish

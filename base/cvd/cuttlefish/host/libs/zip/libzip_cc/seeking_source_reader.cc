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

#include "cuttlefish/host/libs/zip/libzip_cc/seeking_source_reader.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <mutex>

#include "zip.h"

#include "cuttlefish/host/libs/zip/libzip_cc/error.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_reader.h"
#include "cuttlefish/io/fake_pread_pwrite.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<SeekingZipSourceReader> SeekingZipSourceReader::Open(
    SeekableZipSource& source) {
  zip_source_t* raw_source = CF_EXPECT(source.raw_.get());

  CF_EXPECT_EQ(zip_source_open(raw_source), 0, ZipErrorString(raw_source));

  return SeekingZipSourceReader(&source);
}

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

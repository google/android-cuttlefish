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

#include "cuttlefish/host/libs/zip/libzip_cc/reader_seeker_io.h"

#include <stdint.h>

#include <memory>
#include <utility>

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seeking_source_reader.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<ZipReaderSeekerIo> ZipReaderSeekerIo::New(SeekableZipSource source) {
  std::unique_ptr<SeekableZipSource> unique_src =
      std::make_unique<SeekableZipSource>(std::move(source));

  SeekingZipSourceReader reader =
      CF_EXPECT(SeekingZipSourceReader::Open(*unique_src));

  return ZipReaderSeekerIo(std::move(unique_src), std::move(reader));
}

Result<std::unique_ptr<ZipReaderSeekerIo>> ZipReaderSeekerIo::UniquePtr(
    SeekableZipSource src) {
  return std::make_unique<ZipReaderSeekerIo>(CF_EXPECT(New(std::move(src))));
}

ZipReaderSeekerIo::ZipReaderSeekerIo(std::unique_ptr<SeekableZipSource> source,
                                     SeekingZipSourceReader reader)
    : source_(std::move(source)), reader_(std::move(reader)) {}

ZipReaderSeekerIo& ZipReaderSeekerIo::operator=(ZipReaderSeekerIo&& other) {
  if (this != &other) {
    reader_ = std::move(other.reader_);  // destroy the old reader_ first
    source_ = std::move(other.source_);
  }
  return *this;
}

Result<void> ZipReaderSeekerIo::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<uint64_t, int> ZipReaderSeekerIo::Read(void* buf, uint64_t size) {
  return CF_EXPECT(reader_.Read(buf, size));
}

Result<uint64_t, int> ZipReaderSeekerIo::SeekSet(uint64_t offset) {
  return CF_EXPECT(reader_.SeekSet(offset));
}

Result<uint64_t, int> ZipReaderSeekerIo::SeekCur(int64_t offset) {
  return CF_EXPECT(reader_.SeekCur(offset));
}

Result<uint64_t, int> ZipReaderSeekerIo::SeekEnd(int64_t offset) {
  return CF_EXPECT(reader_.SeekEnd(offset));
}

Result<uint64_t, int> ZipReaderSeekerIo::PRead(void* buf, uint64_t count,
                                               uint64_t offset) const {
  return CF_EXPECT(reader_.PRead(buf, count, offset));
}

}  // namespace cuttlefish

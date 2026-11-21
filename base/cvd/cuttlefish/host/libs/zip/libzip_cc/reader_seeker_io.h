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

#pragma once

#include <stdint.h>

#include <memory>

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seeking_source_reader.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class ZipReaderSeekerIo : public ReaderSeeker {
 public:
  static Result<ZipReaderSeekerIo> New(SeekableZipSource);
  static Result<std::unique_ptr<ZipReaderSeekerIo>> UniquePtr(
      SeekableZipSource);

  ZipReaderSeekerIo(ZipReaderSeekerIo&&) = default;
  ~ZipReaderSeekerIo() override = default;
  ZipReaderSeekerIo& operator=(ZipReaderSeekerIo&&);

  Result<void> Visit(IoVisitor& visitor) override;

  Result<uint64_t, int> Read(void* buf, uint64_t size) override;

  Result<uint64_t, int> SeekSet(uint64_t offset) override;

  Result<uint64_t, int> SeekCur(int64_t offset) override;

  Result<uint64_t, int> SeekEnd(int64_t offset) override;

  Result<uint64_t, int> PRead(void* buf, uint64_t count,
                              uint64_t offset) const override;

 private:
  explicit ZipReaderSeekerIo(std::unique_ptr<SeekableZipSource>,
                             SeekingZipSourceReader);

  std::unique_ptr<SeekableZipSource> source_;  // Order matters for destructor
  SeekingZipSourceReader reader_;
};

}  // namespace cuttlefish

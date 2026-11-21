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

#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_reader.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

/* A `SeekableZipSource` in an "open for reading" state. */
class SeekingZipSourceReader : public ZipSourceReader, public ReaderSeeker {
 public:
  /* Makes the source "open for reading." Should not outlive the source. */
  static Result<SeekingZipSourceReader> Open(SeekableZipSource&);

  SeekingZipSourceReader(SeekingZipSourceReader&&) = default;
  ~SeekingZipSourceReader() override = default;
  SeekingZipSourceReader& operator=(SeekingZipSourceReader&&) = default;

  Result<void> Visit(IoVisitor&) override;

  Result<uint64_t, int> Read(void* data, uint64_t length) override;
  Result<uint64_t, int> PRead(void* buf, uint64_t count,
                              uint64_t offset) const override;

  Result<uint64_t, int> SeekSet(uint64_t offset) override;
  Result<uint64_t, int> SeekCur(int64_t offset) override;
  Result<uint64_t, int> SeekEnd(int64_t offset) override;

 private:
  SeekingZipSourceReader(SeekableZipSource*);

  Result<uint64_t, int> Seek(int64_t offset, int whence);
};

}  // namespace cuttlefish

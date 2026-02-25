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

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "cuttlefish/io/io.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_callback.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class SeekableZipSource : public ReadableZipSource {
 public:
  friend class ReadableZip;
  friend class WritableZipSource;

  static Result<SeekableZipSource> FromCallbacks(
      std::unique_ptr<SeekableZipSourceCallback>);

  SeekableZipSource(SeekableZipSource&&) = default;
  ~SeekableZipSource() override = default;
  SeekableZipSource& operator=(SeekableZipSource&&) = default;

  /* Returns a RAII instance that puts this instance in an "open for reading"
   * state. Can fail. Should not outlive this instance. */
  Result<class SeekingZipSourceReader> Reader();

 protected:
  SeekableZipSource(ManagedZipSource);
};

/* A `SeekableZipSource` in an "open for reading" state. */
class SeekingZipSourceReader : public ZipSourceReader, public ReaderSeeker {
 public:
  friend class SeekableZipSource;

  SeekingZipSourceReader(SeekingZipSourceReader&&);
  ~SeekingZipSourceReader() override;
  SeekingZipSourceReader& operator=(SeekingZipSourceReader&&);

  Result<uint64_t> Read(void* data, uint64_t length) override;
  Result<uint64_t> PRead(void* buf, uint64_t count, uint64_t offset) const override;

  Result<uint64_t> SeekSet(uint64_t offset) override;
  Result<uint64_t> SeekCur(int64_t offset) override;
  Result<uint64_t> SeekEnd(int64_t offset) override;

 private:
  Result<uint64_t> Seek(int64_t offset, int whence);

  SeekingZipSourceReader(SeekableZipSource*);
};

Result<std::unique_ptr<ReaderSeeker>> ZipSourceAsReaderSeeker(
    SeekableZipSource inner);

}  // namespace cuttlefish

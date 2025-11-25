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
#include <string>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"

namespace cuttlefish {

class WritableZipSource : public SeekableZipSource {
 public:
  friend class ReadableZip;
  /* References `data`, may not update it on write but `data` should outlive the
   * returned instance. */
  static Result<WritableZipSource> BorrowData(const void* data, size_t size);
  static Result<WritableZipSource> FromFile(const std::string& path);
  /* Data access to an in-memory buffer based on serializing a zip archive. */
  static Result<WritableZipSource> FromZip(class WritableZip);

  WritableZipSource(WritableZipSource&&) = default;
  virtual ~WritableZipSource() = default;
  WritableZipSource& operator=(WritableZipSource&&) = default;

  /* Returns a RAII instance that puts this instance in an "open for writing"
   * state. Can fail. Should not outlive this instance. Cannot be used at the
   * same time as the `Reader()` method from superclasses. */
  Result<class ZipSourceWriter> Writer();

 protected:
  WritableZipSource(ManagedZipSource);
};

/* A `WritableZipSource` in an "open for writing" state. */
class ZipSourceWriter {
 public:
  friend class WritableZipSource;

  ZipSourceWriter(ZipSourceWriter&&);
  ~ZipSourceWriter();
  ZipSourceWriter& operator=(ZipSourceWriter&&);

  /* Writes are not committed until `Finalize` is called. Returns number of
   * bytes written. */
  Result<uint64_t> Write(void* data, uint64_t length);
  Result<void> SeekFromStart(int64_t offset);

  /* Commits writes and closes the writer. */
  static Result<void> Finalize(ZipSourceWriter);

 private:
  ZipSourceWriter(WritableZipSource*);

  WritableZipSource* source_;
};

}  // namespace cuttlefish

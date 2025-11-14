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

#include <string>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"

namespace cuttlefish {

class ReadableZip {
 public:
  friend class WritableZip;
  friend class WritableZipSource;

  static Result<ReadableZip> FromSource(SeekableZipSource);

  ReadableZip(ReadableZip&&);
  virtual ~ReadableZip();
  ReadableZip& operator=(ReadableZip&&);

  /* Counts members, including un-finalized ones from AddFile. */
  Result<uint64_t> NumEntries();

  Result<std::string> EntryName(uint64_t index);
  Result<uint32_t> EntryUnixAttributes(uint64_t index);

  /* Decompresses and extract a file from the archive. */
  Result<SeekableZipSource> GetFile(const std::string& name);
  Result<SeekableZipSource> GetFile(uint64_t index);

 protected:
  ReadableZip(ManagedZip, WritableZipSource);

  ManagedZip raw_;
  WritableZipSource source_;
};

class WritableZip : public ReadableZip {
 public:
  enum class OpenBehavior {
    KeepIfExists,
    Truncate,
  };
  static Result<WritableZip> FromSource(
      WritableZipSource, OpenBehavior open_behavior = OpenBehavior::Truncate);

  WritableZip(WritableZip&&) = default;
  ~WritableZip() override = default;
  WritableZip& operator=(WritableZip&&) = default;

  /* Mutates the archive to add a file. Reading the contents of the sources that
   * are added is deferred until `Finalize` time. */
  Result<void> AddFile(const std::string& name, ReadableZipSource);

  /* Performs transfers from the input `ZipSource`s to the output `ZipSource`
   * and does the archive encoding. */
  static Result<void> Finalize(WritableZip);

 protected:
  static Result<WritableZip> FromSource(WritableZipSource, int flags);

  WritableZip(ManagedZip, WritableZipSource);
};

}  // namespace cuttlefish

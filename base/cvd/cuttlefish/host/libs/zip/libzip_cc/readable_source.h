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
#include <mutex>

#include "zip.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source_callback.h"
#include "cuttlefish/host/libs/zip/libzip_cc/stat.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

int64_t HandleCallback(ReadableZipSourceCallback& callbacks, zip_error_t* error,
                       void* data, uint64_t len, zip_source_cmd_t cmd);

class ReadableZipSource {
 public:
  friend class ReadableZip;
  friend class WritableZip;
  friend class ZipSourceReader;
  friend class SeekingZipSourceReader;
  friend class ZipSourceWriter;

  static Result<ReadableZipSource> FromCallbacks(
      std::unique_ptr<ReadableZipSourceCallback>);

  // Can be safely called with a subclass type.
  ReadableZipSource(ReadableZipSource&&);
  virtual ~ReadableZipSource();
  // Can be safely called with a subclass type.
  ReadableZipSource& operator=(ReadableZipSource&&);

  Result<ZipStat> Stat();

  /* Returns a RAII instance that puts this instance in an "open for reading"
   * state. Can fail. Should not outlive this instance. */
  Result<class ZipSourceReader> Reader();

 protected:
  ManagedZipSource raw_;

  ReadableZipSource(ManagedZipSource);
};

/* A `ReadableZipSource` in an "open for reading" state. */
class ZipSourceReader : public Reader {
 public:
  friend class ReadableZipSource;
  friend class SeekingZipSourceReader;

  ZipSourceReader(ZipSourceReader&&);
  virtual ~ZipSourceReader();
  ZipSourceReader& operator=(ZipSourceReader&&);

  /* Returns a failed Result on error, or a successful result with bytes read or
   * 0 on EOF. */
  Result<uint64_t> Read(void* data, uint64_t length) override;

 protected:
  std::recursive_mutex mutex_;
 private:
  ZipSourceReader(ReadableZipSource*);

  ReadableZipSource* source_;
};

}  // namespace cuttlefish

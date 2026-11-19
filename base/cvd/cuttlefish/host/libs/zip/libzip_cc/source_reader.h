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

#include <mutex>

#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/stat.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

/* A `ReadableZipSource` in an "open for reading" state. */
class ZipSourceReader : public Reader {
 public:
  /* Makes the source "open for reading." Should not outlive the source. */
  static Result<ZipSourceReader> Open(ReadableZipSource&);

  ZipSourceReader(ZipSourceReader&&);
  virtual ~ZipSourceReader();
  ZipSourceReader& operator=(ZipSourceReader&&);

  /* Returns a failed Result on error, or a successful result with bytes read or
   * 0 on EOF. */
  Result<uint64_t, int> Read(void* data, uint64_t length) override;

  Result<void> Visit(IoVisitor&) override;

 protected:
  std::recursive_mutex mutex_;

 private:
  friend class SeekingZipSourceReader;

  ZipSourceReader(ReadableZipSource*);

  ReadableZipSource* source_;
};

}  // namespace cuttlefish

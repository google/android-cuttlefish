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

#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/stat.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class ReadableZipSource {
 public:
  // Can be safely called with a subclass type.
  ReadableZipSource(ReadableZipSource&&);
  virtual ~ReadableZipSource();
  // Can be safely called with a subclass type.
  ReadableZipSource& operator=(ReadableZipSource&&);

  Result<ZipStat> Stat();

 private:
  friend class ReadableZip;
  friend class SeekableZipSource;
  friend class SeekingZipSourceReader;
  friend class WritableZip;
  friend class WritableZipSource;
  friend class ZipSourceReader;
  friend class ZipSourceWriter;

  ReadableZipSource(ManagedZipSource);

  ManagedZipSource raw_;
};

}  // namespace cuttlefish

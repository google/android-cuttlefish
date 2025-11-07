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

namespace cuttlefish {

class ReadableZipSourceCallback {
 public:
  virtual ~ReadableZipSourceCallback() = default;

  virtual bool Close() = 0;
  virtual bool Open() = 0;
  virtual int64_t Read(char* data, uint64_t len) = 0;
  virtual uint64_t Size() = 0;
};

/* Callback interface to provide file data to libzip. */
class SeekableZipSourceCallback : public ReadableZipSourceCallback {
 public:
  virtual ~SeekableZipSourceCallback() = default;

  virtual bool SetOffset(int64_t offset) = 0;
  virtual int64_t Offset() = 0;
};

}  // namespace cuttlefish

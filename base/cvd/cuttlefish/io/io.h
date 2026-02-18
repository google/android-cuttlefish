//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class Reader {
 public:
  virtual ~Reader() = default;

  // Has the semantics of read(2)
  virtual Result<uint64_t> Read(void* buf, uint64_t count) = 0;
};

class Writer {
 public:
  virtual ~Writer() = default;

  // Has the semantics of write(2)
  virtual Result<uint64_t> Write(const void* buf, uint64_t count) = 0;
};

class Seeker {
 public:
  virtual ~Seeker() = default;

  // Has the semantics of lseek(2) with SEEK_SET
  virtual Result<uint64_t> SeekSet(uint64_t offset) = 0;
  // Has the semantics of lseek(2) with SEEK_CUR
  virtual Result<uint64_t> SeekCur(int64_t offset) = 0;
  // Has the semantics of lseek(2) with SEEK_END
  virtual Result<uint64_t> SeekEnd(int64_t offset) = 0;
};

class ReaderSeeker : public Reader, public Seeker {
 public:
  // Has the semantics of pread(2)
  virtual Result<uint64_t> PRead(void* buf, uint64_t count,
                                 uint64_t offset) const = 0;
};

class WriterSeeker : public Writer, public Seeker {
 public:
  // Has the semantics of pwrite(2)
  virtual Result<uint64_t> PWrite(const void* buf, uint64_t count,
                                  uint64_t offset) = 0;
};

class ReaderWriterSeeker : public ReaderSeeker, public WriterSeeker {
 public:
  // Members redeclared to avoid compilation errors like "non-static member
  // 'Xyz' found in multiple base-class subobjects"
  virtual Result<uint64_t> Read(void* buf, uint64_t count) = 0;
  virtual Result<uint64_t> Write(const void* buf, uint64_t count) = 0;
  virtual Result<uint64_t> SeekSet(uint64_t offset) = 0;
  virtual Result<uint64_t> SeekCur(int64_t offset) = 0;
  virtual Result<uint64_t> SeekEnd(int64_t offset) = 0;
  virtual Result<uint64_t> PRead(void* buf, uint64_t count,
                                 uint64_t offset) const = 0;
  virtual Result<uint64_t> PWrite(const void* buf, uint64_t count,
                                  uint64_t offset) = 0;
};

}  // namespace cuttlefish

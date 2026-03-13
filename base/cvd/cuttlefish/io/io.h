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

class ConcatReaderSeeker;
class IoVisitor;
class ReadWindowView;
class SharedFdIo;

/** Used to determine the runtime type of an IO instance object. */
class IoVisitable {
 public:
  virtual ~IoVisitable() = default;

  virtual Result<void> Visit(IoVisitor&) = 0;
};

class Reader : public virtual IoVisitable {
 public:
  virtual ~Reader() = default;

  Result<void> Visit(IoVisitor&) override;
  // Has the semantics of read(2)
  virtual Result<uint64_t> Read(void* buf, uint64_t count) = 0;
};

class Writer : public virtual IoVisitable {
 public:
  virtual ~Writer() = default;

  Result<void> Visit(IoVisitor&) override;
  // Has the semantics of write(2)
  virtual Result<uint64_t> Write(const void* buf, uint64_t count) = 0;
};

class Seeker : public virtual IoVisitable {
 public:
  virtual ~Seeker() = default;

  Result<void> Visit(IoVisitor&) override;
  // Has the semantics of lseek(2) with SEEK_SET
  virtual Result<uint64_t> SeekSet(uint64_t offset) = 0;
  // Has the semantics of lseek(2) with SEEK_CUR
  virtual Result<uint64_t> SeekCur(int64_t offset) = 0;
  // Has the semantics of lseek(2) with SEEK_END
  virtual Result<uint64_t> SeekEnd(int64_t offset) = 0;
};

class ReaderSeeker : public Reader, public Seeker {
 public:
  Result<void> Visit(IoVisitor&) override;
  // Has the semantics of pread(2)
  virtual Result<uint64_t> PRead(void* buf, uint64_t count,
                                 uint64_t offset) const = 0;
};

class WriterSeeker : public Writer, public Seeker {
 public:
  Result<void> Visit(IoVisitor&) override;
  // Has the semantics of pwrite(2)
  virtual Result<uint64_t> PWrite(const void* buf, uint64_t count,
                                  uint64_t offset) = 0;
  virtual Result<void> Truncate(uint64_t size);
};

class ReaderWriterSeeker : public ReaderSeeker, public WriterSeeker {
 public:
  // Members redeclared to avoid compilation errors like "non-static member
  // 'Xyz' found in multiple base-class subobjects"
  Result<void> Visit(IoVisitor&) override;

  Result<uint64_t> Read(void* buf, uint64_t count) override = 0;
  Result<uint64_t> Write(const void* buf, uint64_t count) override = 0;
  Result<uint64_t> SeekSet(uint64_t offset) override = 0;
  Result<uint64_t> SeekCur(int64_t offset) override = 0;
  Result<uint64_t> SeekEnd(int64_t offset) override = 0;
  Result<uint64_t> PRead(void* buf, uint64_t count,
                         uint64_t offset) const override = 0;
  Result<uint64_t> PWrite(const void* buf, uint64_t count,
                          uint64_t offset) override = 0;
  Result<void> Truncate(uint64_t size) override;
};

/**
 * Defines callbacks which receive the runtime type of an IoVisitable.
 *
 * Types outside this file only have forward declarations to avoid circular
 * dependencies. Consider using DefaultIoVisitor when writing an implementation.
 *
 * Not every IoVisitable is explicitly listed here. The Accept method for the
 * nearest known type will be called instead. Some types are unreachable because
 * they are implemented inside C++ source files only, and some are unlisted
 * because no visitor implementation needs special logic for them.
 */
class IoVisitor {
 public:
  virtual ~IoVisitor() = default;

  virtual Result<void> Accept(ConcatReaderSeeker&) = 0;
  virtual Result<void> Accept(ReadWindowView&) = 0;
  virtual Result<void> Accept(Reader&) = 0;
  virtual Result<void> Accept(ReaderSeeker&) = 0;
  virtual Result<void> Accept(ReaderWriterSeeker&) = 0;
  virtual Result<void> Accept(Seeker&) = 0;
  virtual Result<void> Accept(SharedFdIo&) = 0;
  virtual Result<void> Accept(Writer&) = 0;
  virtual Result<void> Accept(WriterSeeker&) = 0;
};

}  // namespace cuttlefish

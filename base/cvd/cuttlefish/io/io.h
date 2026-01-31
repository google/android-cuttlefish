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

  virtual Result<size_t> PartialRead(void* buf, size_t count) = 0;
};

class Writer {
 public:
  virtual ~Writer() = default;

  virtual Result<size_t> PartialWrite(const void* buf, size_t count) = 0;
};

class Seeker {
 public:
  virtual ~Seeker() = default;

  virtual Result<size_t> SeekSet(size_t offset) = 0;
  virtual Result<size_t> SeekCur(ssize_t offset) = 0;
  virtual Result<size_t> SeekEnd(ssize_t offset) = 0;
};

class ReaderSeeker : public Reader, virtual public Seeker {
 public:
  virtual Result<size_t> PartialReadAt(void* buf, size_t count,
                                       size_t offset) const = 0;
};

class WriterSeeker : public Writer, virtual public Seeker {
 public:
  virtual Result<size_t> PartialWriteAt(const void* buf, size_t count,
                                        size_t offset) const = 0;
};

}  // namespace cuttlefish

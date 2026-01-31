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
  virtual Result<size_t> Read(void* buf, size_t count) = 0;
};

class Writer {
 public:
  virtual Result<size_t> Write(const void* buf, size_t count) = 0;
};

class Seeker {
 public:
  virtual Result<size_t> SeekSet(size_t offset) = 0;
  virtual Result<size_t> SeekCur(ssize_t offset) = 0;
  virtual Result<size_t> SeekEnd(ssize_t offset) = 0;
};

class ReaderSeeker : virtual public Reader, virtual public Seeker {
 public:
  virtual Result<size_t> PRead(void* buf, size_t count,
                               size_t offset) const = 0;
};

class SeekerWriter : virtual public Seeker, virtual public Writer {
 public:
  virtual Result<size_t> PWrite(const void* buf, size_t count,
                                size_t offset) const = 0;
};

class ReaderWriter : virtual public Reader, virtual public Writer {};

class ReaderSeekerWriter : virtual public ReaderSeeker,
                           virtual public SeekerWriter {};

}  // namespace cuttlefish

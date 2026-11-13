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

#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/visitable.h"
#include "cuttlefish/io/writer_seeker.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class ReaderWriterSeeker : public ReaderSeeker, public WriterSeeker {
 public:
  // Members redeclared to avoid compilation errors like "non-static member
  // 'Xyz' found in multiple base-class subobjects"
  Result<void> Visit(IoVisitor&) override = 0;

  Result<uint64_t, int> Read(void* buf, uint64_t count) override = 0;
  Result<uint64_t, int> Write(const void* buf, uint64_t count) override = 0;
  Result<uint64_t, int> SeekSet(uint64_t offset) override = 0;
  Result<uint64_t, int> SeekCur(int64_t offset) override = 0;
  Result<uint64_t, int> SeekEnd(int64_t offset) override = 0;
  Result<uint64_t, int> PRead(void* buf, uint64_t count,
                              uint64_t offset) const override = 0;
  Result<uint64_t, int> PWrite(const void* buf, uint64_t count,
                               uint64_t offset) override = 0;
  Result<void> Truncate(uint64_t size) override = 0;
};

}  // namespace cuttlefish

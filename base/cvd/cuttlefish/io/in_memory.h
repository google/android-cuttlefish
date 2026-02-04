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

#include <shared_mutex>
#include <vector>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class InMemoryIo : public ReaderSeeker, public WriterSeeker {
 public:
  InMemoryIo() = default;
  explicit InMemoryIo(std::vector<char>);

  Result<uint64_t> Read(void* buf, uint64_t count) override;
  Result<uint64_t> Write(const void* buf, uint64_t count) override;
  Result<uint64_t> SeekSet(uint64_t offset) override;
  Result<uint64_t> SeekCur(int64_t offset) override;
  Result<uint64_t> SeekEnd(int64_t offset) override;
  Result<uint64_t> PRead(void* buf, uint64_t count,
                         uint64_t offset) const override;
  Result<uint64_t> PWrite(const void* buf, uint64_t count,
                          uint64_t offset) override;

 private:
  uint64_t ClampRange(uint64_t begin, uint64_t length) const;
  void GrowTo(uint64_t);

  std::vector<char> data_;
  uint64_t cursor_ = 0;
  mutable std::shared_mutex mutex_;
};

}  // namespace cuttlefish

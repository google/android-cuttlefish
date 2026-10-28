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

class InMemoryIo : public ReaderWriterSeeker {
 public:
  InMemoryIo(std::vector<char>& data, std::shared_mutex& mutex);

  Result<uint64_t> Read(void* buf, uint64_t count) override;

  Result<uint64_t, int> Write(const void* buf, uint64_t count) override;

  Result<uint64_t, int> SeekSet(uint64_t offset) override;

  Result<uint64_t, int> SeekCur(int64_t offset) override;

  Result<uint64_t, int> SeekEnd(int64_t offset) override;

  Result<uint64_t> PRead(void* buf, uint64_t count,
                         uint64_t offset) const override;

  Result<uint64_t, int> PWrite(const void* buf, uint64_t count,
                               uint64_t offset) override;

 private:
  // Must be called with the lock held for reading or writing
  uint64_t ClampRange(uint64_t begin, uint64_t length) const;

  // Must be called with the lock held for writing
  void GrowTo(uint64_t new_size);

  std::vector<char>& data_;
  uint64_t cursor_ = 0;
  std::shared_mutex& mutex_;
};

}  // namespace cuttlefish

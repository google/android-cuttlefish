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

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class ReaderFakeSeeker : public ReaderSeeker {
 public:
  ReaderFakeSeeker(uint64_t length);

  Result<uint64_t> Read(void* buf, uint64_t count) final override;
  Result<uint64_t> SeekSet(uint64_t) final override;
  Result<uint64_t> SeekCur(int64_t) final override;
  Result<uint64_t> SeekEnd(int64_t) final override;

  // Subclasses only need to implement PRead
 private:
  uint64_t seek_pos_ = 0;
  uint64_t length_;
};

}  // namespace cuttlefish

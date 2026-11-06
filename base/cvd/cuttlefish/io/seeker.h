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

#include "cuttlefish/io/visitable.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class Seeker : public virtual IoVisitable {
 public:
  virtual ~Seeker() = default;

  // Has the semantics of lseek(2) with SEEK_SET
  virtual Result<uint64_t, int> SeekSet(uint64_t offset) = 0;
  // Has the semantics of lseek(2) with SEEK_CUR
  virtual Result<uint64_t, int> SeekCur(int64_t offset) = 0;
  // Has the semantics of lseek(2) with SEEK_END
  virtual Result<uint64_t, int> SeekEnd(int64_t offset) = 0;
};

}  // namespace cuttlefish

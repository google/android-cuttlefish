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

#include "cuttlefish/io/length.h"

#include <stdint.h>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<uint64_t> Length(Seeker& seeker) {
  uint64_t current_pos = CF_EXPECT(seeker.SeekCur(0));
  uint64_t end = CF_EXPECT(seeker.SeekEnd(0));
  CF_EXPECT(seeker.SeekSet(current_pos));
  return end;
}

Result<uint64_t> Length(ReaderWriterSeeker& seeker) {
  return CF_EXPECT(Length(static_cast<ReaderSeeker&>(seeker)));
}

}  // namespace cuttlefish

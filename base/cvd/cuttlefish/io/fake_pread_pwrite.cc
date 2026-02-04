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

#include "cuttlefish/io/io.h"

#include <stdint.h>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<uint64_t> FakePRead(ReaderSeeker& reader_seeker, void* buf, uint64_t count, uint64_t offset) {
  size_t original_offset = CF_EXPECT(reader_seeker.SeekCur(0));
  CF_EXPECT(reader_seeker.SeekSet(offset));
  Result<size_t> read_res = reader_seeker.Read(buf, count);
  CF_EXPECT(reader_seeker.SeekSet(original_offset));
  return CF_EXPECT(std::move(read_res));
}

}  // namespace cuttlefish

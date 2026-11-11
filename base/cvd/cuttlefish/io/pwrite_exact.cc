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

#include "cuttlefish/io/pwrite_exact.h"

#include <errno.h>
#include <stdint.h>

#include "absl/strings/string_view.h"

#include "cuttlefish/io/writer_seeker.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void, int> PWriteExact(WriterSeeker& writer, const void* buf,
                              uint64_t size, uint64_t offset) {
  while (size > 0) {
    uint64_t data_written = CF_EXPECT(writer.PWrite(buf, size, offset));
    CF_EXPECTVF(data_written > 0, EIO,
                "Write returned 0 before completing, still have {}", size);
    buf = reinterpret_cast<const uint8_t*>(buf) + data_written;
    offset += data_written;
    size -= data_written;
  }
  return {};
}

Result<void, int> PWriteExact(WriterSeeker& writer, const absl::string_view str,
                              const uint64_t offset) {
  CF_EXPECT(PWriteExact(writer, str.data(), str.size(), offset));
  return {};
}

}  // namespace cuttlefish

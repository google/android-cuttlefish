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

#include "cuttlefish/io/write_exact.h"

#include <stdint.h>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> WriteExact(Writer& writer, const char* buf, size_t size) {
  while (size > 0) {
    size_t data_written = CF_EXPECT(writer.Write((const void*)buf, size));
    CF_EXPECT_GT(data_written, 0, "Write returned 0 before completing");
    buf += data_written;
    size -= data_written;
  }
  return {};
}

}  // namespace cuttlefish

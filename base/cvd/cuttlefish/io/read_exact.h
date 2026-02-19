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
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> PReadExact(ReaderSeeker&, char* buf, size_t size, uint64_t offset);

template <typename T>
Result<T> PReadExactBinary(ReaderSeeker& reader, uint64_t offset) {
  T data;
  char* const data_char = reinterpret_cast<char*>(&data);
  CF_EXPECT(PReadExact(reader, data_char, sizeof(data), offset));
  return data;
}

}  // namespace cuttlefish

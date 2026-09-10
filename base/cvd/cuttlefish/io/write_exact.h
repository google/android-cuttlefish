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

#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> WriteExact(Writer&, const char* buf, size_t size);
Result<void> WriteExact(Writer&, const char* buf);  // Assumes null termination
Result<void> WriteExact(Writer&, const std::string&);
Result<void> WriteExact(Writer&, const std::vector<char>&);
Result<void> WriteExact(Writer&, std::string_view);

template <typename T>
Result<void> WriteExactBinary(Writer& writer, const T& data) {
  const char* const data_char = reinterpret_cast<const char*>(&data);
  return WriteExact(writer, data_char, sizeof(data));
}

}  // namespace cuttlefish

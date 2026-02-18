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

#include "cuttlefish/io/string.h"

#include <stdint.h>

#include <sstream>
#include <string>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::string> ReadToString(Reader& reader, size_t buffer_size) {
  std::stringstream out;

  std::vector<char> buf(1 << 16);
  uint64_t data_read;
  while ((data_read = CF_EXPECT(reader.Read(buf.data(), buf.size()))) > 0) {
    out.write(buf.data(), data_read);
  }
  return out.str();
}

}  // namespace cuttlefish

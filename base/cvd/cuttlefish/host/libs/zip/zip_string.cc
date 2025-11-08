//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/libs/zip/zip_string.h"

#include <stdint.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/libzip_cc/source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/zip_cc.h"

namespace cuttlefish {

Result<std::string> ReadToString(ReadableZipSource& source) {
  ZipSourceReader reader = CF_EXPECT(source.Reader());
  return CF_EXPECT(ReadToString(reader));
}

Result<std::string> ReadToString(ZipSourceReader& reader) {
  std::stringstream out;

  std::vector<char> buf(1 << 16);
  uint64_t data_read;
  while ((data_read = CF_EXPECT(reader.Read(buf.data(), buf.size()))) > 0) {
    out.write(buf.data(), data_read);
  }
  return out.str();
}

Result<void> AddStringAt(WritableZip& zip, const std::string& data,
                         const std::string& zip_path) {
  ReadableZipSource source =
      CF_EXPECT(WritableZipSource::BorrowData(data.data(), data.size()));
  CF_EXPECT(zip.AddFile(zip_path, std::move(source)));
  return {};
}

}  // namespace cuttlefish

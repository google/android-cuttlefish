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

#include <string>
#include <utility>

#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::string> ReadToString(ReadableZipSource& source) {
  ZipSourceReader reader = CF_EXPECT(source.Reader());
  return CF_EXPECT(ReadToString(reader));
}

Result<void> AddStringAt(WritableZip& zip, const std::string& data,
                         const std::string& zip_path) {
  ReadableZipSource source =
      CF_EXPECT(WritableZipSource::BorrowData(data.data(), data.size()));
  CF_EXPECT(zip.AddFile(zip_path, std::move(source)));
  return {};
}

}  // namespace cuttlefish

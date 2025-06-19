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

#include "cuttlefish/host/libs/zip/zip_file.h"

#include <string>
#include <utility>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {

Result<ReadableZip> ZipOpenRead(const std::string& fs_path) {
  return CF_EXPECT(ZipOpenReadWrite(fs_path));
}

Result<WritableZip> ZipOpenReadWrite(const std::string& fs_path) {
  WritableZipSource source = CF_EXPECT(WritableZipSource::FromFile(fs_path));
  return CF_EXPECT(WritableZip::FromSource(std::move(source)));
}

Result<void> AddFile(WritableZip& zip, const std::string& fs_path) {
  CF_EXPECT(AddFileAt(zip, fs_path, fs_path));
  return {};
}

Result<void> AddFileAt(WritableZip& zip, const std::string& fs_path,
                       const std::string& zip_path) {
  CF_EXPECTF(FileExists(fs_path), "No file in the filesystem at '{}'", fs_path);
  ReadableZipSource source = CF_EXPECT(WritableZipSource::FromFile(fs_path));
  CF_EXPECT(zip.AddFile(zip_path, std::move(source)));
  return {};
}

}  // namespace cuttlefish

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

#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

#include <string>
#include <utility>

#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/libzip_cc/stat.h"
#include "cuttlefish/host/libs/zip/libzip_cc/zip_cc.h"
#include "cuttlefish/host/libs/zip/zip_copy.h"

namespace cuttlefish {

Result<ReadableZip> ZipOpenRead(const std::string& fs_path) {
  return CF_EXPECT(ZipOpenReadWrite(fs_path));
}

Result<WritableZip> ZipOpenReadWrite(const std::string& fs_path) {
  WritableZipSource source = CF_EXPECT(WritableZipSource::FromFile(fs_path));
  return CF_EXPECT(WritableZip::FromSource(
      std::move(source), WritableZip::OpenBehavior::KeepIfExists));
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

Result<void> ExtractFile(ReadableZip& zip, const std::string& zip_path,
                         const std::string& host_path) {
  ReadableZipSource source = CF_EXPECT(zip.GetFile(zip_path));
  WritableZipSource dest = CF_EXPECT(WritableZipSource::FromFile(host_path));
  CF_EXPECT(Copy(source, dest));

  ZipStat stat_out = CF_EXPECT(source.Stat());
  uint64_t index = CF_EXPECT(std::move(stat_out.index));

  Result<uint32_t> attributes = zip.EntryUnixAttributes(index);
  if (attributes.ok()) {
    uint32_t mode = (*attributes >> 16) & 0777;
    CF_EXPECT_EQ(chmod(host_path.c_str(), mode), 0, StrError(errno));
  }
  return {};
}

}  // namespace cuttlefish

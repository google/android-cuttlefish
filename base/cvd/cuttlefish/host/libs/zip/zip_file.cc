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

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/io/copy.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

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

Result<void> ExtractFile(ReadableZip& zip, std::string_view zip_path,
                         const std::string& host_path) {
  std::unique_ptr<ReaderSeeker> reader = CF_EXPECT(zip.OpenReadOnly(zip_path));
  CF_EXPECT(reader.get());

  WritableZipSource dest = CF_EXPECT(WritableZipSource::FromFile(host_path));
  ZipSourceWriter writer = CF_EXPECT(dest.Writer());

  CF_EXPECT(Copy(*reader, writer));
  CF_EXPECT(ZipSourceWriter::Finalize(std::move(writer)));

  if (Result<uint32_t> attr = zip.FileAttributes(zip_path); attr.ok()) {
    CF_EXPECT_EQ(chmod(host_path.c_str(), *attr), 0, StrError(errno));
  }
  return {};
}

}  // namespace cuttlefish

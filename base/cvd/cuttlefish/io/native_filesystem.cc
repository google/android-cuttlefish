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

#include "cuttlefish/io/native_filesystem.h"

#include <fcntl.h>
#include <stdint.h>

#include <memory>
#include <string_view>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/stat.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::unique_ptr<ReaderWriterSeeker>> NativeFilesystem::CreateFile(
    std::string_view path) {
  return std::make_unique<Fd>(
      CF_EXPECT(Fd::Open(path, O_CLOEXEC | O_CREAT | O_EXCL | O_RDWR, 0644)));
}

Result<uint32_t> NativeFilesystem::FileAttributes(std::string_view path) const {
  return CF_EXPECT(Stat(path)).st_mode;
}

Result<void> NativeFilesystem::DeleteFile(std::string_view path) {
  CF_EXPECT(RemoveFile(path));
  return {};
}

Result<std::unique_ptr<ReaderSeeker>> NativeFilesystem::OpenReadOnly(
    std::string_view path) {
  return std::make_unique<Fd>(CF_EXPECT(Fd::Open(path, O_CLOEXEC | O_RDONLY)));
}

Result<std::unique_ptr<ReaderWriterSeeker>> NativeFilesystem::OpenReadWrite(
    std::string_view path) {
  return std::make_unique<Fd>(CF_EXPECT(Fd::Open(path, O_CLOEXEC | O_RDWR)));
}

}  // namespace cuttlefish

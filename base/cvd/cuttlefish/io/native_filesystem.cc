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

#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include <string_view>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/io/shared_fd.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::unique_ptr<ReaderWriterSeeker>> NativeFilesystem::CreateFile(
    std::string_view path) {
  SharedFD fd =
      SharedFD::Open(std::string(path), O_CLOEXEC | O_CREAT | O_EXCL | O_RDWR);
  CF_EXPECTF(fd->IsOpen(),
             "Failed to open '{}' with O_CREAT | O_EXCL | O_RDWR: '{}'", path,
             fd->StrError());
  return std::make_unique<SharedFdIo>(fd);
}

Result<void> NativeFilesystem::DeleteFile(std::string_view path) {
  CF_EXPECT_GE(unlink(std::string(path).c_str()), 0, StrError(errno));
  return {};
}

Result<std::unique_ptr<ReaderSeeker>> NativeFilesystem::OpenReadOnly(
    std::string_view path) {
  SharedFD fd = SharedFD::Open(std::string(path), O_CLOEXEC | O_RDONLY);
  CF_EXPECTF(fd->IsOpen(), "Failed to open '{}' with O_RDONLY: '{}'", path,
             fd->StrError());
  return std::make_unique<SharedFdIo>(fd);
}

Result<std::unique_ptr<ReaderWriterSeeker>> NativeFilesystem::OpenReadWrite(
    std::string_view path) {
  SharedFD fd = SharedFD::Open(std::string(path), O_CLOEXEC | O_RDWR);
  CF_EXPECTF(fd->IsOpen(), "Failed to open '{}' with O_RDWR: '{}'", path,
             fd->StrError());
  return std::make_unique<SharedFdIo>(fd);
}

}  // namespace cuttlefish

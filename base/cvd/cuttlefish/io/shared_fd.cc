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

#include "cuttlefish/io/shared_fd.h"

#include <stdint.h>
#include <unistd.h>

#include <utility>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

SharedFdIo::SharedFdIo(SharedFD fd) : fd_(std::move(fd)) {}

Result<size_t> SharedFdIo::PartialRead(void* buf, size_t count) {
  ssize_t data_read = fd_->Read(buf, count);
  CF_EXPECT_GE(data_read, 0, fd_->StrError());
  return data_read;
}

Result<size_t> SharedFdIo::PartialWrite(const void* buf, size_t count) {
  ssize_t data_written = fd_->Write(buf, count);
  CF_EXPECT_GE(data_written, 0, fd_->StrError());
  return data_written;
}

Result<size_t> SharedFdIo::SeekSet(size_t offset) {
  CF_EXPECT_EQ(fd_->LSeek(offset, SEEK_SET), offset, fd_->StrError());
  return offset;
}

Result<size_t> SharedFdIo::SeekCur(ssize_t offset) {
  ssize_t new_offset = fd_->LSeek(offset, SEEK_CUR);
  CF_EXPECT_GE(new_offset, 0, fd_->StrError());
  return new_offset;
}

Result<size_t> SharedFdIo::SeekEnd(ssize_t offset) {
  ssize_t new_offset = fd_->LSeek(offset, SEEK_END);
  CF_EXPECT_GE(new_offset, 0, fd_->StrError());
  return new_offset;
}

Result<size_t> SharedFdIo::PartialReadAt(void* buf, size_t count,
                                         size_t offset) const {
  ssize_t data_read = fd_->PRead(buf, count, offset);
  CF_EXPECT_GE(data_read, 0, fd_->StrError());
  return data_read;
}

Result<size_t> SharedFdIo::PartialWriteAt(const void* buf, size_t count,
                                          size_t offset) {
  ssize_t data_written = fd_->PWrite(buf, count, offset);
  CF_EXPECT_GE(data_written, 0, fd_->StrError());
  return data_written;
}

}  // namespace cuttlefish

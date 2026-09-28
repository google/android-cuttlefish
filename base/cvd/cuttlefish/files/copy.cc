/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/files/copy.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>

#include <string_view>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace {

Result<void> CopyImpl(std::string_view from, std::string_view to) {
  Fd fd_from = CF_EXPECT(Fd::Open(from, O_RDONLY));
  Fd fd_to = CF_EXPECT(Fd::Open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644));

  off_t farthest_seek = CF_EXPECT(fd_from.SeekEnd(0));
  CF_EXPECT(fd_to.Truncate(farthest_seek));

  off_t offset = 0;
  while (offset < farthest_seek) {
    off_t new_offset = fd_from.LSeek(offset, SEEK_HOLE);
    if (new_offset == -1) {
      CF_EXPECTF(fd_from.GetErrno() == ENXIO, "Could not lseek in '{}': {}",
                 from, fd_from.StrError());
      return {};
    }
    off_t data_bytes = new_offset - offset;

    CF_EXPECT(fd_to.SeekSet(offset));

    CF_EXPECTF(fd_to.SendFile(fd_from, &offset, data_bytes),
               "SendFile failed: {}", fd_to.StrError());

    CF_EXPECT_EQ(offset, new_offset);

    if (offset >= farthest_seek) {
      return {};
    }
    offset = fd_from.LSeek(offset, SEEK_DATA);
    if (offset == -1) {
      CF_EXPECTF(fd_from.GetErrno() == ENXIO, "Could not lseek in '{}': {}",
                 from, fd_from.StrError());
      return {};
    }
  }
  return {};
}

}  // namespace

Result<void> Copy(std::string_view from, std::string_view to) {
  CF_EXPECTF(CopyImpl(from, to), "Failed to copy file '{}' to '{}'", from, to);
  return {};
}

}  // namespace cuttlefish

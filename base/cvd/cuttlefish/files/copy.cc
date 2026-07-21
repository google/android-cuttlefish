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

#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"

namespace cuttlefish {

bool Copy(const std::string& from, const std::string& to) {
  SharedFD fd_from = SharedFD::Open(from, O_RDONLY);
  SharedFD fd_to = SharedFD::Open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  if (!fd_from->IsOpen() || !fd_to->IsOpen()) {
    return false;
  }

  off_t farthest_seek = fd_from->LSeek(0, SEEK_END);
  if (farthest_seek == -1) {
    LOG(ERROR) << "Could not lseek in \"" << from
               << "\": " << fd_from->StrError();
    return false;
  }
  if (fd_to->Truncate(farthest_seek) < 0) {
    LOG(ERROR) << "Failed to truncate " << to << ": " << fd_to->StrError();
  }
  off_t offset = 0;
  while (offset < farthest_seek) {
    off_t new_offset = fd_from->LSeek(offset, SEEK_HOLE);
    if (new_offset == -1) {
      if (fd_from->GetErrno() == ENXIO) {
        return true;
      }
      LOG(ERROR) << "Could not lseek in \"" << from
                 << "\": " << fd_from->StrError();
      return false;
    }
    auto data_bytes = new_offset - offset;
    if (fd_to->LSeek(offset, SEEK_SET) < 0) {
      LOG(ERROR) << "lseek() on " << to << " failed: " << fd_to->StrError();
      return false;
    }
    if (!fd_to->SendFile(*fd_from, &offset, data_bytes)) {
      LOG(ERROR) << "SendFile failed: " << fd_to->StrError();
      return false;
    }
    CHECK_EQ(offset, new_offset);

    if (offset >= farthest_seek) {
      return true;
    }
    new_offset = fd_from->LSeek(offset, SEEK_DATA);
    if (new_offset == -1) {
      if (fd_from->GetErrno() == ENXIO) {
        return true;
      }
      LOG(ERROR) << "Could not lseek in \"" << from
                 << "\": " << fd_from->StrError();
      return false;
    }
    offset = new_offset;
  }
  return true;
}

}  // namespace cuttlefish

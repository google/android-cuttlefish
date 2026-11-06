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

#include "cuttlefish/files/create_or_reuse_and_drain_fifo.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/posix/stat.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/temp_failure_retry.h"  // IWYU pragma: keep
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<Fd> CreateOrReuseAndDrainFifo(const std::string& path, mode_t mode) {
  Result<struct stat> st = Stat(path);
  if (st.has_value()) {
    CF_EXPECTF(S_ISFIFO(st->st_mode), "File at '{}' exists but is not a FIFO",
               path);
  } else {
    CF_EXPECTF(TEMP_FAILURE_RETRY(mkfifo(path.c_str(), mode)) == 0,
               "Failed to mkfifo('{}', {:o}): {}", path, mode, StrError());
  }

  Fd ret = CF_EXPECT(Fd::Open(path, O_RDWR));

  if (st.has_value()) {
    int flags = ret.Fcntl(F_GETFL, 0);
    if (flags >= 0) {
      ret.Fcntl(F_SETFL, flags | O_NONBLOCK);
      char buf[4096];
      while (ret.Read(buf, sizeof(buf)).value_or(0) > 0) {
        // Reading while there is data to read
      }
      ret.Fcntl(F_SETFL, flags);
    }
  }

  return ret;
}

}  // namespace cuttlefish

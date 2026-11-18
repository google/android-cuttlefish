/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "cuttlefish/host/commands/cvdalloc/sem.h"

#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>

#include <chrono>
#include <optional>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/command_util/util.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish::cvdalloc {

Result<void> Post(const SharedFD socket) {
  char i = 0;
  CF_EXPECT(socket->Write(&i, sizeof(char)) != -1);
  return {};
}

Result<void> Wait(const SharedFD socket, std::chrono::seconds timeout) {
  CF_EXPECT(socket->IsOpen(), "IsOpen: " << socket->StrError());
  CF_EXPECT(WaitForRead(socket, timeout.count()), "WaitForRead");
  char i = -1;
  uint64_t r = CF_EXPECT(socket->Read(&i, sizeof(i)));
  CF_EXPECT_GT(r, 0, socket->StrError());
  CF_EXPECT(i == 0, "Unexpected read");
  return {};
}

Result<std::optional<int>> Wait(const SharedFD socket,
                                std::chrono::seconds timeout,
                                const SharedFD signals) {
  std::vector<PollSharedFd> fds = {
      {socket, POLLIN, 0},   // NOLINT(misc-include-cleaner)
      {signals, POLLIN, 0},  // NOLINT(misc-include-cleaner)
  };
  constexpr size_t kSignalsIndex = 1;
  const int timeout_ms =
      timeout <= kSemNoTimeout
          ? -1
          : static_cast<int>(std::chrono::milliseconds(timeout).count());
  while (true) {
    const int ready = SharedFD::Poll(fds, timeout_ms);
    if (ready == -1 && errno == EINTR) {
      continue;
    }
    CF_EXPECT(ready != 0, "Timed out");
    CF_EXPECTF(ready > 0, "poll: {}", StrError());
    if (fds[kSignalsIndex].revents != 0) {
      char signum = 0;
      const size_t read = CF_EXPECT(signals->Read(&signum, sizeof(signum)));
      CF_EXPECT_EQ(read, sizeof(signum));
      return static_cast<int>(signum);
    }
    CF_EXPECT(Wait(socket, timeout));
    return std::nullopt;
  }
}

}  // namespace cuttlefish::cvdalloc

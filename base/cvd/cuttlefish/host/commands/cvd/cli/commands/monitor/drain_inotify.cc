/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/drain_inotify.h"

#include <errno.h>
#include <stdint.h>
#include <sys/inotify.h>
#include <sys/types.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

/*
 * Exhaustively drain all available events from the non-blocking descriptor to
 * coalesce rapid file modifications.
 *
 * Returns a bit mask of the events unioned together.
 */
Result<uint32_t> DrainInotifyEvents(SharedFD inotify_fd) {
  uint32_t events = 0;

  char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
  ssize_t read_res = 0;
  while ((read_res = inotify_fd->Read(buf, sizeof(buf))) > 0) {
    char* ptr = buf;
    while (ptr < buf + read_res) {
      inotify_event* event = reinterpret_cast<inotify_event*>(ptr);
      events |= event->mask;
      ptr += sizeof(inotify_event) + event->len;
    }
  }
  CF_EXPECT(read_res != 0, "Unexpected End-of-File reading inotify descriptor");
  const int err = inotify_fd->GetErrno();
  CF_EXPECTF(err == EAGAIN || err == EWOULDBLOCK,
             "Unexpected error reading inotify descriptor: {}", StrError(err));
  return events;
}

}  // namespace cuttlefish

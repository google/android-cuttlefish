/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <limits.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string>
#include <vector>

#include <android-base/logging.h>

#include "inotify.h"

namespace cuttlefish {

std::vector<std::string> GetCreatedFileListFromInotifyFd(int fd) {
  return GetFileListFromInotifyFd(fd, IN_CREATE);
}

#define INOTIFY_MAX_EVENT_SIZE (sizeof(struct inotify_event) + NAME_MAX + 1)

std::vector<std::string> GetFileListFromInotifyFd(int fd, uint32_t mask) {
  char event_readout[INOTIFY_MAX_EVENT_SIZE];
  int bytes_parsed = 0;
  std::vector<std::string> result;
  // Each successful read can contain one or more of inotify_event events
  // Note: read() on inotify returns 'whole' events, will never partially
  // populate the buffer.
  int event_read_out_length = read(fd, event_readout, INOTIFY_MAX_EVENT_SIZE);

  if (event_read_out_length == -1) {
    LOG(ERROR) << __FUNCTION__
               << ": Couldn't read out inotify event due to error: '"
               << strerror(errno) << "' (" << errno << ")";
    return std::vector<std::string>();
  }

  while (bytes_parsed < event_read_out_length) {
    struct inotify_event* event =
        reinterpret_cast<inotify_event*>(event_readout + bytes_parsed);
    bytes_parsed += sizeof(struct inotify_event) + event->len;

    // No file name was present
    if (event->len == 0) {
      LOG(ERROR) << __FUNCTION__ << ": inotify event didn't contain filename";
      continue;
    }
    if (!(event->mask & mask)) {
      LOG(ERROR) << __FUNCTION__
                 << ": inotify event didn't pertain to the event";
      continue;
    }
    result.push_back(std::string(event->name));
  }

  return result;
}

}  // namespace cuttlefish

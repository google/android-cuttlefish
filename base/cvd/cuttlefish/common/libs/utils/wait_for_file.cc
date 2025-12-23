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

#include "cuttlefish/common/libs/utils/wait_for_file.h"

#ifdef __linux__
#include <sys/inotify.h>
#endif
#include <sys/select.h>

#include <chrono>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/unique_fd.h>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/inotify.h"
#include "cuttlefish/result/result.h"

#ifdef __linux__

namespace cuttlefish {
namespace {
class InotifyWatcher {
 public:
  InotifyWatcher(int inotify, const std::string& path, int watch_mode)
      : inotify_(inotify) {
    watch_ = inotify_add_watch(inotify_, path.c_str(), watch_mode);
  }
  virtual ~InotifyWatcher() { inotify_rm_watch(inotify_, watch_); }

 private:
  int inotify_;
  int watch_;
};

Result<void> WaitForFileInternal(const std::string& path, int timeoutSec,
                                 int inotify) {
  CF_EXPECT_NE(path, "", "Path is empty");

  if (FileExists(path, true)) {
    return {};
  }

  const auto targetTime =
      std::chrono::system_clock::now() + std::chrono::seconds(timeoutSec);

  const std::string parentPath = android::base::Dirname(path);
  const std::string filename = android::base::Basename(path);

  CF_EXPECT(WaitForFile(parentPath, timeoutSec),
            "Error while waiting for parent directory creation");

  auto watcher = InotifyWatcher(inotify, parentPath.c_str(), IN_CREATE);

  if (FileExists(path, true)) {
    return {};
  }

  while (true) {
    const auto currentTime = std::chrono::system_clock::now();

    if (currentTime >= targetTime) {
      return CF_ERR("Timed out");
    }

    const auto timeRemain =
        std::chrono::duration_cast<std::chrono::microseconds>(targetTime -
                                                              currentTime)
            .count();
    const auto secondInUsec =
        std::chrono::microseconds(std::chrono::seconds(1)).count();
    struct timeval timeout;

    timeout.tv_sec = timeRemain / secondInUsec;
    timeout.tv_usec = timeRemain % secondInUsec;

    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(inotify, &readfds);

    auto ret = select(inotify + 1, &readfds, NULL, NULL, &timeout);

    if (ret == 0) {
      return CF_ERR("select() timed out");
    } else if (ret < 0) {
      return CF_ERRNO("select() failed");
    }

    auto names = GetCreatedFileListFromInotifyFd(inotify);

    CF_EXPECT(!names.empty(),
              "Failed to get names from inotify " << strerror(errno));

    if (Contains(names, filename)) {
      return {};
    }
  }

  return CF_ERR("This shouldn't be executed");
}

}  // namespace

Result<void> WaitForFile(const std::string& path, int timeoutSec) {
  android::base::unique_fd inotify(inotify_init1(IN_CLOEXEC));

  CF_EXPECT(WaitForFileInternal(path, timeoutSec, inotify.get()));

  return {};
}

#endif

}  // namespace cuttlefish

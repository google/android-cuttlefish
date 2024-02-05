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

#include "host/libs/image_aggregator/sparse_image_utils.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <string.h>
#include <sys/inotify.h>

#include <fstream>

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

const int MAX_BUF_SIZE = sizeof(struct inotify_event) + NAME_MAX + 1;
const char ANDROID_SPARSE_IMAGE_MAGIC[] = "\x3A\xFF\x26\xED";
namespace cuttlefish {

bool IsSparseImage(const std::string& image_path) {
  std::ifstream file(image_path, std::ios::binary);
  if (!file) {
    LOG(FATAL) << "Could not open '" << image_path << "'";
    return false;
  }
  char buffer[5] = {0};
  file.read(buffer, 4);
  file.close();
  return strcmp(ANDROID_SPARSE_IMAGE_MAGIC, buffer) == 0;
}

bool ConvertToRawImage(const std::string& image_path) {
  std::string tmp_lock_image_path = image_path + ".lock";
  const auto parentPath = android::base::Dirname(image_path);
  int timer = 10;
  const auto targetTime =
      std::chrono::system_clock::now() + std::chrono::seconds(timer);
  android::base::unique_fd inotify_fd(inotify_init1(IN_CLOEXEC));
  int inotify = inotify_fd.get();
  SharedFD fd;
  char buf[MAX_BUF_SIZE]
      __attribute__((aligned(__alignof__(struct inotify_event))));
  auto watch =
      inotify_add_watch(inotify, parentPath.c_str(), IN_CREATE | IN_DELETE);
  if (watch == -1) {
    LOG(FATAL) << "Unable to add watch, directory may not exist, "
               << parentPath;
    return false;
  }
  while (true) {
    fd = SharedFD::Open(tmp_lock_image_path.c_str(),
                        O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd->IsOpen()) {
      break;
    }
    // May need a timeout.
    const auto currentTime = std::chrono::system_clock::now();
    if (currentTime >= targetTime) {
      LOG(FATAL) << "Unable to convert Android sparse image " << image_path
                 << " to raw image. Timeout";
      return false;
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
      LOG(FATAL) << "select() timed out";
      return false;
    } else if (ret < 0) {
      LOG(FATAL) << "select() failed";
      return false;
    }
    // The function should read the event after select()
    int length = read(inotify_fd, buf, MAX_BUF_SIZE);
    if (length < 0) {
      LOG(FATAL) << "read lock file failed";
      return false;
    }
  }
  inotify_rm_watch(inotify, watch);
  if (!IsSparseImage(image_path)) {
    // Release lock before return
    remove(tmp_lock_image_path.c_str());
    LOG(DEBUG) << "Skip non-sparse image " << image_path;
    return false;
  }

  auto simg2img_path = HostBinaryPath("simg2img");
  Command simg2img_cmd(simg2img_path);
  std::string tmp_raw_image_path = image_path + ".raw";
  simg2img_cmd.AddParameter(image_path);
  simg2img_cmd.AddParameter(tmp_raw_image_path);

  // Use simg2img to convert sparse image to raw image.
  int success = simg2img_cmd.Start().Wait();
  if (success != 0) {
    // Release lock before FATAL and return
    remove(tmp_lock_image_path.c_str());
    LOG(FATAL) << "Unable to convert Android sparse image " << image_path
               << " to raw image. " << success;
    return false;
  }

  // Replace the original sparse image with the raw image.
  if (unlink(image_path.c_str()) != 0) {
    // Release lock before FATAL
    remove(tmp_lock_image_path.c_str());
    PLOG(FATAL) << "Unable to delete original sparse image";
  }

  Command mv_cmd("/bin/mv");
  mv_cmd.AddParameter("-f");
  mv_cmd.AddParameter(tmp_raw_image_path);
  mv_cmd.AddParameter(image_path);
  success = mv_cmd.Start().Wait();
  // Release lock and then leave critical section
  remove(tmp_lock_image_path.c_str());
  if (success != 0) {
    LOG(FATAL) << "Unable to rename raw image " << success;
    return false;
  }

  return true;
}

}  // namespace cuttlefish

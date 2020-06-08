/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <cutils/properties.h>

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#include <climits>
#include <cerrno>
#include <string>

// Taken from android::base, which wasn't available on platform versions
// earlier than nougat.

static bool ReadFdToString(int fd, std::string* content) {
  content->clear();
  char buf[BUFSIZ];
  ssize_t n;
  while ((n = TEMP_FAILURE_RETRY(read(fd, &buf[0], sizeof(buf)))) > 0) {
    content->append(buf, n);
  }
  return (n == 0) ? true : false;
}

static bool ReadFileToString(const std::string& path, std::string* content,
                             bool follow_symlinks) {
  int flags = O_RDONLY | O_CLOEXEC | (follow_symlinks ? 0 : O_NOFOLLOW);
  int fd = TEMP_FAILURE_RETRY(open(path.c_str(), flags));
  if (fd == -1) {
    return false;
  }
  bool result = ReadFdToString(fd, content);
  close(fd);
  return result;
}

int main(int argc __unused, char *argv[] __unused) {
  const char sysfs_base[] = "/sys/class/virtio-ports/";
  DIR *dir = opendir(sysfs_base);
  if (dir) {
    dirent *dp;
    while ((dp = readdir(dir)) != nullptr) {
      std::string dirname = dp->d_name;
      std::string sysfs(sysfs_base + dirname + "/name");
      struct stat st;
      if (stat(sysfs.c_str(), &st)) {
        continue;
      }
      std::string content;
      if (!ReadFileToString(sysfs, &content, true)) {
        continue;
      }
      if (content.empty()) {
        continue;
      }
      content.erase(content.end() - 1);
      // Leaves 32-11=22 characters for the port name from QEMU.
      std::string dev("/dev/" + dirname);
      std::string propname("vendor.ser." + content);
      property_set(propname.c_str(), dev.c_str());
    }
  }
  return 0;
}

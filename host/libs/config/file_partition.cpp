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
#include <errno.h>
#include <limits.h>

#include <sstream>

#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/config/file_partition.h"

namespace config {
namespace {
constexpr char kTempFileSuffix[] = ".img";

void UpdateACLs(const std::string& path) {
  std::string command;

  command = "setfacl -m u:libvirt-qemu:rw '" + path + "'";
  CHECK(system(command.c_str()) == 0)
      << "Could not set ACLs for partition image " << path << ": "
      << strerror(errno);

  command = "setfacl -m u:$(whoami):rw '" + path + "'";
  CHECK(system(command.c_str()) == 0)
      << "Could not set ACLs for partition image " << path << ": "
      << strerror(errno);
}

void Initialize(const std::string& path) {
  std::string command = "/sbin/mkfs.ext4 -F '" + path + "' &>/dev/null";
  CHECK(system(command.c_str()) == 0)
      << "Could not initialize filesystem on partition image " << path << ": "
      << strerror(errno);
}
}  // namespace

FilePartition::~FilePartition() {
  if (should_delete_) {
    LOG(INFO) << "Deleting partition image file " << name_;
    errno = 0;
    unlink(name_.c_str());
    if (errno != 0) {
      LOG(WARNING) << "Could not delete partition image file: "
                   << strerror(errno);
    }
  }
}

std::unique_ptr<FilePartition> FilePartition::ReuseExistingFile(
    const std::string& path) {
  cvd::SharedFD fd(cvd::SharedFD::Open(path.c_str(), O_RDWR));
  CHECK(fd->IsOpen()) << "Could not open file: " << path << ": "
                      << fd->StrError();

  UpdateACLs(path);
  return std::unique_ptr<FilePartition>(new FilePartition(path, false));
}

std::unique_ptr<FilePartition> FilePartition::CreateNewFile(
    const std::string& path, int size_mb) {
  {
    cvd::SharedFD fd(cvd::SharedFD::Open(path.c_str(), O_CREAT | O_RDWR, 0600));
    CHECK(fd->IsOpen()) << "Could not open file: " << path << ": "
                        << fd->StrError();
    CHECK(fd->Truncate(size_mb << 20) == 0)
        << "Could not truncate file " << path << ": " << fd->StrError();
  }

  UpdateACLs(path);
  Initialize(path);
  return std::unique_ptr<FilePartition>(new FilePartition(path, false));
}

// Create temporary FilePartition object using supplied prefix.
// Newly created file will be deleted after this instance is destroyed.
std::unique_ptr<FilePartition> FilePartition::CreateTemporaryFile(
    const std::string& prefix, int size_mb) {
  std::stringstream ss;
  ss << prefix << "-XXXXXX" << kTempFileSuffix;
  char path[PATH_MAX];
  strncpy(&path[0], ss.str().c_str(), sizeof(path));

  {
    int raw_fd = mkostemps(&path[0], strlen(kTempFileSuffix), O_RDWR | O_CREAT);
    CHECK(raw_fd > 0) << "Could not create temporary file: " << strerror(errno);
    CHECK(ftruncate(raw_fd, size_mb << 20) == 0)
        << "Could not truncate file " << path << ": " << strerror(errno);
    close(raw_fd);
  }

  UpdateACLs(path);
  Initialize(path);
  return std::unique_ptr<FilePartition>(new FilePartition(path, true));
}

}  // namespace config

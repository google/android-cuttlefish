/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/instance_lock.h"

#include <sys/file.h>

#include <sstream>
#include <string>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

InstanceLockFile::InstanceLockFile(SharedFD fd, int instance_num)
    : fd_(fd), instance_num_(instance_num) {}

int InstanceLockFile::Instance() const { return instance_num_; }

Result<InUseState> InstanceLockFile::Status() const {
  CF_EXPECT(fd_->LSeek(0, SEEK_SET), fd_->StrError());
  char state_char;
  CF_EXPECT(fd_->Read(&state_char, 1) == 1, fd_->StrError());
  switch (state_char) {
    case static_cast<char>(InUseState::kInUse):
      return InUseState::kInUse;
    case static_cast<char>(InUseState::kNotInUse):
      return InUseState::kNotInUse;
    default:
      return CF_ERR("Unexpected state value \"" << state_char << "\"");
  }
}

Result<void> InstanceLockFile::Status(InUseState state) {
  CF_EXPECT(fd_->LSeek(0, SEEK_SET), fd_->StrError());
  char state_char = static_cast<char>(state);
  CF_EXPECT(fd_->Write(&state_char, 1) == 1, fd_->StrError());
  return {};
}

bool InstanceLockFile::operator<(const InstanceLockFile& other) const {
  if (instance_num_ != other.instance_num_) {
    return instance_num_ < other.instance_num_;
  }
  return fd_ < other.fd_;
}

InstanceLockFileManager::InstanceLockFileManager() = default;

// Replicates tempfile.gettempdir() in Python
static std::string TempDir() {
  std::vector<std::string> try_dirs = {
      StringFromEnv("TMPDIR", ""),
      StringFromEnv("TEMP", ""),
      StringFromEnv("TMP", ""),
      "/tmp",
      "/var/tmp",
      "/usr/tmp",
  };
  for (const auto& try_dir : try_dirs) {
    if (DirectoryExists(try_dir)) {
      return try_dir;
    }
  }
  return CurrentDirectory();
}

static Result<SharedFD> OpenLockFile(int instance_num) {
  std::stringstream path;
  path << TempDir() << "/acloud_cvd_temp/";
  path << "local-instance-" << instance_num << ".lock";
  auto fd = SharedFD::Open(path.str(), O_CREAT | O_RDWR, 0666);
  CF_EXPECT(fd->IsOpen(), "open(\"" << path.str() << "\"): " << fd->StrError());
  return fd;
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireLock(
    int instance_num) {
  auto fd = CF_EXPECT(OpenLockFile(instance_num));
  CF_EXPECT(fd->Flock(LOCK_EX), fd->StrError());
  return InstanceLockFile(fd, instance_num);
}

Result<std::set<InstanceLockFile>> InstanceLockFileManager::AcquireLocks(
    const std::set<int>& instance_nums) {
  std::set<InstanceLockFile> locks;
  for (const auto& num : instance_nums) {
    locks.emplace(CF_EXPECT(AcquireLock(num)));
  }
  return locks;
}

Result<std::optional<InstanceLockFile>> InstanceLockFileManager::TryAcquireLock(
    int instance_num) {
  auto fd = CF_EXPECT(OpenLockFile(instance_num));
  int flock_result = fd->Flock(LOCK_EX | LOCK_NB);
  if (flock_result == 0) {
    return InstanceLockFile(fd, instance_num);
  } else if (flock_result == -1 && fd->GetErrno() == EWOULDBLOCK) {
    return {};
  }
  return CF_ERR("flock " << instance_num << " failed: " << fd->StrError());
}

Result<std::set<InstanceLockFile>> InstanceLockFileManager::TryAcquireLocks(
    const std::set<int>& instance_nums) {
  std::set<InstanceLockFile> locks;
  for (const auto& num : instance_nums) {
    auto lock = CF_EXPECT(TryAcquireLock(num));
    if (lock) {
      locks.emplace(std::move(*lock));
    }
  }
  return locks;
}

}  // namespace cuttlefish

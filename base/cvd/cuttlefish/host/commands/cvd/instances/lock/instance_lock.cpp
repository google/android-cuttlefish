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

#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"

#include <sys/file.h>

#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

InstanceLockFile::InstanceLockFile(LockFile&& lock_file, const int instance_num)
    : lock_file_(std::move(lock_file)), instance_num_(instance_num) {}

int InstanceLockFile::Instance() const { return instance_num_; }

Result<InUseState> InstanceLockFile::Status() const {
  auto in_use_state = CF_EXPECT(lock_file_.Status());
  return in_use_state;
}

Result<void> InstanceLockFile::Status(InUseState state) {
  CF_EXPECT(lock_file_.Status(state));
  return {};
}

InstanceLockFileManager::InstanceLockFileManager(
    std::string instance_locks_path)
    : instance_locks_path_(std::move(instance_locks_path)) {};

Result<std::string> InstanceLockFileManager::LockFilePath(int instance_num) {
  std::stringstream path;
  path << instance_locks_path_;
  CF_EXPECT(EnsureDirectoryExists(path.str()));
  path << "local-instance-" << instance_num << ".lock";
  return path.str();
}

Result<void> InstanceLockFileManager::RemoveLockFile(int instance_num) {
  const auto lock_file_path = CF_EXPECT(LockFilePath(instance_num));
  CF_EXPECT(RemoveFile(lock_file_path), StrError(errno));
  return {};
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireUnusedLock() {
  for (int i = 1;; i++) {
    auto lock = CF_EXPECT(TryAcquireLock(i));
    if (lock && CF_EXPECT(lock->Status()) == InUseState::kNotInUse) {
      return *lock;
    }
  }
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireLock(
    int instance_num) {
  const auto lock_file_path = CF_EXPECT(LockFilePath(instance_num));
  LockFile lock_file =
      CF_EXPECT(lock_file_manager_.AcquireLock(lock_file_path));
  return InstanceLockFile(std::move(lock_file), instance_num);
}

Result<std::optional<InstanceLockFile>> InstanceLockFileManager::TryAcquireLock(
    int instance_num) {
  const auto lock_file_path = CF_EXPECT(LockFilePath(instance_num));
  std::optional<LockFile> lock_file_opt =
      CF_EXPECT(lock_file_manager_.TryAcquireLock(lock_file_path));
  if (!lock_file_opt) {
    return std::nullopt;
  }
  return InstanceLockFile(std::move(*lock_file_opt), instance_num);
}

}  // namespace cuttlefish

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

#include <sys/types.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

InstanceLockFile::InstanceLockFile(LockFile&& lock_file,
                                   const unsigned instance_num)
    : lock_file_(std::move(lock_file)), instance_num_(instance_num) {}

unsigned InstanceLockFile::Instance() const { return instance_num_; }

Result<InUseState> InstanceLockFile::Status() const {
  return CF_EXPECT(lock_file_.Status());
}

Result<std::optional<uid_t>> InstanceLockFile::HolderUid() const {
  return CF_EXPECT(lock_file_.HolderUid());
}

Result<void> InstanceLockFile::Status(const InUseState state) {
  CF_EXPECT(lock_file_.Status(state));
  return {};
}

InstanceLockFileManager::InstanceLockFileManager(
    std::string instance_locks_path)
    : instance_locks_path_(std::move(instance_locks_path)) {}

std::string InstanceLockFileManager::LockFilePath(
    const unsigned instance_num) const {
  const absl::string_view slash =
      absl::EndsWith(instance_locks_path_, "/") ? "" : "/";
  return absl::StrCat(instance_locks_path_, slash, "local-instance-",
                      instance_num, ".lock");
}

Result<void> InstanceLockFileManager::ResetLockFile(
    const unsigned instance_num) {
  if (!FileExists(LockFilePath(instance_num))) {
    return {};
  }
  InstanceLockFile lock = CF_EXPECT(AcquireLock(instance_num));
  if (CF_EXPECT(lock.Status()) == InUseState::kInUse &&
      CF_EXPECT(lock.HolderUid()).value_or(geteuid()) == geteuid()) {
    CF_EXPECT(lock.Status(InUseState::kNotInUse));
  }
  return {};
}

Result<void> InstanceLockFileManager::ResetAllOwnedLocks() {
  const uid_t self_uid = geteuid();
  for (unsigned i = 1; FileExists(LockFilePath(i)); ++i) {
    Result<std::optional<InstanceLockFile>> lock = TryAcquireLock(i);
    if (!lock.has_value()) {
      LOG(WARNING) << "Failed to inspect lock " << i << ": " << lock.error();
      continue;
    }
    if (!*lock) {
      continue;
    }
    const Result<std::optional<uid_t>> holder = (*lock)->HolderUid();
    if (!holder.has_value()) {
      LOG(WARNING) << "Failed to read holder UID for lock " << i << ": "
                   << holder.error();
      continue;
    }
    if (*holder == self_uid) {
      if (const Result<void> res = (*lock)->Status(InUseState::kNotInUse);
          !res.has_value()) {
        LOG(WARNING) << "Failed to reset lock " << i << ": " << res.error();
      }
    }
  }
  return {};
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireUnusedLock() {
  for (unsigned i = 1;; ++i) {
    if (!FileExists(LockFilePath(i))) {
      if (i == 1) {
        CF_EXPECT(TryAcquireLock(1));
      }
      return CF_ERRF("No unused instance locks available in '{}'",
                     instance_locks_path_);
    }
    Result<std::optional<InstanceLockFile>> lock = TryAcquireLock(i);
    if (!lock.has_value()) {
      LOG(WARNING) << "Skipping lock " << i << ": " << lock.error();
      continue;
    }
    if (!*lock) {
      continue;
    }
    const Result<InUseState> status = (*lock)->Status();
    if (!status.has_value()) {
      LOG(WARNING) << "Skipping lock " << i << ": " << status.error();
      continue;
    }
    if (*status == InUseState::kNotInUse) {
      return std::move(**lock);
    }
  }
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireLock(
    const unsigned instance_num) {
  LockFile lock_file =
      CF_EXPECT(lock_file_manager_.AcquireLock(LockFilePath(instance_num)));
  return InstanceLockFile(std::move(lock_file), instance_num);
}

Result<std::optional<InstanceLockFile>> InstanceLockFileManager::TryAcquireLock(
    const unsigned instance_num) {
  std::optional<LockFile> lock_file_opt =
      CF_EXPECT(lock_file_manager_.TryAcquireLock(LockFilePath(instance_num)));
  if (!lock_file_opt) {
    return std::nullopt;
  }
  return InstanceLockFile(std::move(*lock_file_opt), instance_num);
}

}  // namespace cuttlefish

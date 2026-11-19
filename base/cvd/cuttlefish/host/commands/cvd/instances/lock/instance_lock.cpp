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
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

bool DefaultCreateLocksOnDemand() {
#ifdef __linux__
  return InSandbox();
#else
  return true;
#endif
}

}  // namespace

InstanceLockFile::InstanceLockFile(LockFile&& lock_file,
                                   const unsigned instance_num)
    : lock_file_(std::move(lock_file)), instance_num_(instance_num) {}

unsigned InstanceLockFile::Instance() const { return instance_num_; }

Result<InUseState> InstanceLockFile::Status() const {
  auto in_use_state = CF_EXPECT(lock_file_.Status());
  return in_use_state;
}

Result<std::optional<uid_t>> InstanceLockFile::HolderUid() const {
  return CF_EXPECT(lock_file_.HolderUid());
}

Result<void> InstanceLockFile::Status(InUseState state) {
  CF_EXPECT(lock_file_.Status(state));
  return {};
}

InstanceLockFileManager::InstanceLockFileManager(
    std::string instance_locks_path)
    : InstanceLockFileManager(std::move(instance_locks_path),
                              DefaultCreateLocksOnDemand()) {}

InstanceLockFileManager::InstanceLockFileManager(
    std::string instance_locks_path, bool create_missing_locks)
    : instance_locks_path_(std::move(instance_locks_path)),
      create_missing_locks_(create_missing_locks),
      lock_file_manager_(create_missing_locks) {}

std::string InstanceLockFileManager::LockFilePath(unsigned instance_num) const {
  const bool trailing_slash = absl::EndsWith(instance_locks_path_, "/");
  return absl::StrCat(instance_locks_path_, trailing_slash ? "" : "/",
                      "local-instance-", instance_num, ".lock");
}

Result<void> InstanceLockFileManager::RemoveLockFile(unsigned instance_num) {
  const std::string lock_file_path = LockFilePath(instance_num);
  if (!FileExists(lock_file_path)) {
    return {};
  }
  InstanceLockFile lock = CF_EXPECT(AcquireLock(instance_num));
  if (CF_EXPECT(lock.Status()) == InUseState::kNotInUse) {
    return {};
  }
  std::optional<uid_t> holder_uid = CF_EXPECT(lock.HolderUid());
  if (holder_uid.has_value() && *holder_uid != geteuid()) {
    return {};
  }
  CF_EXPECT(lock.Status(InUseState::kNotInUse));
  return {};
}

Result<void> InstanceLockFileManager::ResetAllOwnedLocks() {
  if (!DirectoryExists(instance_locks_path_)) {
    return {};
  }
  std::vector<std::string> entries =
      CF_EXPECT(DirectoryContents(instance_locks_path_));
  const uid_t self_uid = geteuid();
  for (const std::string& entry : entries) {
    absl::string_view name = entry;
    if (!absl::ConsumePrefix(&name, "local-instance-") ||
        !absl::ConsumeSuffix(&name, ".lock")) {
      continue;
    }
    unsigned id = 0;
    if (!absl::SimpleAtoi(name, &id) || id == 0) {
      continue;
    }
    Result<std::optional<InstanceLockFile>> lock = TryAcquireLock(id);
    if (!lock.has_value() || !lock->has_value()) {
      continue;
    }
    Result<InUseState> status = (*lock)->Status();
    if (!status.has_value() || *status != InUseState::kInUse) {
      continue;
    }
    Result<std::optional<uid_t>> holder = (*lock)->HolderUid();
    if (holder.has_value() && holder->has_value() && **holder == self_uid) {
      CF_EXPECT((*lock)->Status(InUseState::kNotInUse));
    }
  }
  return {};
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireUnusedLock() {
  for (unsigned i = 1;; i++) {
    if (i > 1 && !create_missing_locks_ && !FileExists(LockFilePath(i))) {
      return CF_ERRF("No unused instance locks available in '{}'",
                     instance_locks_path_);
    }
    auto lock = CF_EXPECT(TryAcquireLock(i));
    if (lock && CF_EXPECT(lock->Status()) == InUseState::kNotInUse) {
      return *lock;
    }
  }
}

Result<InstanceLockFile> InstanceLockFileManager::AcquireLock(
    unsigned instance_num) {
  const std::string lock_file_path = LockFilePath(instance_num);
  LockFile lock_file =
      CF_EXPECT(lock_file_manager_.AcquireLock(lock_file_path));
  return InstanceLockFile(std::move(lock_file), instance_num);
}

Result<std::optional<InstanceLockFile>> InstanceLockFileManager::TryAcquireLock(
    unsigned instance_num) {
  const std::string lock_file_path = LockFilePath(instance_num);
  std::optional<LockFile> lock_file_opt =
      CF_EXPECT(lock_file_manager_.TryAcquireLock(lock_file_path));
  if (!lock_file_opt) {
    return std::nullopt;
  }
  return InstanceLockFile(std::move(*lock_file_opt), instance_num);
}

}  // namespace cuttlefish

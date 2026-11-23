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
#pragma once

#include <sys/types.h>

#include <optional>
#include <string>

#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// This class is not thread safe.
class InstanceLockFile {
  friend class InstanceLockFileManager;

 public:
  unsigned Instance() const;
  Result<InUseState> Status() const;
  // Returns the UID recorded with kInUse, or nullopt if unused or unattributed.
  Result<std::optional<uid_t>> HolderUid() const;
  Result<void> Status(InUseState);

 private:
  InstanceLockFile(LockFile&& lock_file, unsigned instance_num);
  LockFile lock_file_;
  const unsigned instance_num_;
};

// Coordinates instance locks via pre-created host lock files.
class InstanceLockFileManager {
 public:
  explicit InstanceLockFileManager(std::string instance_locks_path);
  InstanceLockFileManager(const InstanceLockFileManager&) = delete;
  InstanceLockFileManager& operator=(const InstanceLockFileManager&) = delete;
  InstanceLockFileManager(InstanceLockFileManager&&) = default;
  InstanceLockFileManager& operator=(InstanceLockFileManager&&) = default;

  Result<InstanceLockFile> AcquireLock(unsigned instance_num);
  Result<InstanceLockFile> AcquireUnusedLock();

  // Resets an owned or unattributed lock to kNotInUse in place (never unlinks).
  Result<void> ResetLockFile(unsigned instance_num);
  // Resets all locks owned by the current effective UID to kNotInUse in place.
  Result<void> ResetAllOwnedLocks();

 private:
  std::string LockFilePath(unsigned instance_num) const;
  Result<std::optional<InstanceLockFile>> TryAcquireLock(unsigned instance_num);

  std::string instance_locks_path_;
  LockFileManager lock_file_manager_;
};

}  // namespace cuttlefish

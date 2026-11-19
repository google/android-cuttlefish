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
  Result<std::optional<uid_t>> HolderUid() const;
  Result<void> Status(InUseState);

 private:
  InstanceLockFile(LockFile&& lock_file, unsigned instance_num);
  LockFile lock_file_;
  const unsigned instance_num_;
};

class InstanceLockFileManager {
 public:
  InstanceLockFileManager(std::string instance_locks_path);
  InstanceLockFileManager(std::string instance_locks_path,
                          bool create_missing_locks);

  Result<InstanceLockFile> AcquireLock(unsigned instance_num);
  Result<InstanceLockFile> AcquireUnusedLock();

  // Marks the pre-created lock file for `instance_num` as `kNotInUse` while
  // holding its exclusive flock if not held by a different UID.
  Result<void> RemoveLockFile(unsigned instance_num);

  // Resets all unheld lock files in `instance_locks_path_` recorded as held by
  // `geteuid()` back to `kNotInUse`.
  Result<void> ResetAllOwnedLocks();

 private:
  std::string LockFilePath(unsigned instance_num) const;
  Result<std::optional<InstanceLockFile>> TryAcquireLock(unsigned instance_num);

  std::string instance_locks_path_;
  bool create_missing_locks_;
  LockFileManager lock_file_manager_;
};

}  // namespace cuttlefish

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

#include <optional>
#include <string>

#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"

namespace cuttlefish {

// This class is not thread safe.
class InstanceLockFile {
  friend class InstanceLockFileManager;
  using LockFile = cvd_impl::LockFile;

 public:
  int Instance() const;
  Result<InUseState> Status() const;
  Result<void> Status(InUseState);

 private:
  InstanceLockFile(LockFile&& lock_file, int instance_num);
  LockFile lock_file_;
  const int instance_num_;
};

class InstanceLockFileManager {
  using LockFile = cvd_impl::LockFile;
  using LockFileManager = cvd_impl::LockFileManager;

 public:
  InstanceLockFileManager(std::string instance_locks_path);

  Result<InstanceLockFile> AcquireLock(int instance_num);
  Result<InstanceLockFile> AcquireUnusedLock();

  // TODO: This routine should  be removed and replaced with allocd
  // The caller must check if the instance_num belongs to the user, before
  // calling this. It is a quick fix for b/316824572
  Result<void> RemoveLockFile(int instance_num);

 private:
  Result<std::string> LockFilePath(int instance_num);
  Result<std::optional<InstanceLockFile>> TryAcquireLock(int instance_num);

  std::string instance_locks_path_;
  LockFileManager lock_file_manager_;
};

}  // namespace cuttlefish

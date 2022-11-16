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

#include <functional>
#include <optional>
#include <set>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

class InstanceLockFileManager;

enum class InUseState : char {
  kInUse = 'I',
  kNotInUse = 'N',
};

// Replicates tempfile.gettempdir() in Python
std::string TempDir();

// This class is not thread safe.
class InstanceLockFile {
 public:
  int Instance() const;
  Result<InUseState> Status() const;
  Result<void> Status(InUseState);

  bool operator<(const InstanceLockFile&) const;

 private:
  friend class InstanceLockFileManager;

  InstanceLockFile(SharedFD fd, int instance_num);

  SharedFD fd_;
  int instance_num_;
};

class InstanceLockFileManager {
 public:
  INJECT(InstanceLockFileManager());

  Result<InstanceLockFile> AcquireLock(int instance_num);
  Result<std::set<InstanceLockFile>> AcquireLocks(const std::set<int>& nums);

  Result<std::optional<InstanceLockFile>> TryAcquireLock(int instance_num);
  Result<std::set<InstanceLockFile>> TryAcquireLocks(const std::set<int>& nums);

  // Best-effort attempt to find a free instance id.
  Result<std::optional<InstanceLockFile>> TryAcquireUnusedLock();

  Result<std::vector<InstanceLockFile>> LockAllAvailable();

 private:
  /*
   * Generate value to initialize
   */
  Result<std::set<int>> FindPotentialInstanceNumsFromNetDevices();
  std::optional<std::set<int>> all_instance_nums_;
};

}  // namespace cuttlefish

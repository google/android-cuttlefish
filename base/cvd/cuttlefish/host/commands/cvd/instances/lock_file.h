/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <memory>
#include <optional>
#include <set>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

enum class InUseState : char {
  kInUse = 'I',
  kNotInUse = 'N',
};

// Replicates tempfile.gettempdir() in Python
std::string TempDir();

namespace cvd_impl {

// This class is not thread safe.
class LockFile {
  friend class LockFileManager;

  /* b/303724170
   *
   * Unfortunately, the LockFile implementation forgot to release
   * the Flock() it got on destruction. Also, unfortunately, the
   * LockFile is being copied as we have used std::optional
   * here and there, which means LockFile objects are copied.
   *
   * The goal is to disable LockFile(const LockFile&) and the
   * copy assignment operator. For now, however, we add to LockFile
   * a shared_ptr of this class, so that at the very last LockFile
   * object for a given lock is destroyed, the LockFileReleaser will
   * releases the lock.
   *
   * Must be created only in the LockFile constructors
   */
  class LockFileReleaser {
   public:
    LockFileReleaser(const SharedFD& fd, const std::string& lock_file_path);
    ~LockFileReleaser();

   private:
    SharedFD flocked_file_fd_;
    const std::string lock_file_path_;  // for error message
  };

 public:
  const auto& LockFilePath() const { return lock_file_path_; }
  Result<InUseState> Status() const;
  Result<void> Status(InUseState);

  // to put this into a set
  bool operator<(const LockFile& other) const;

 private:
  LockFile(SharedFD fd, const std::string& lock_file_path);

  SharedFD fd_;
  const std::string lock_file_path_;
  std::shared_ptr<LockFileReleaser> lock_file_lock_releaser_;
};

class LockFileManager {
 public:
  LockFileManager() = default;

  Result<LockFile> AcquireLock(const std::string& lock_file_path);
  Result<std::set<LockFile>> AcquireLocks(
      const std::set<std::string>& lock_file_paths);

  Result<std::optional<LockFile>> TryAcquireLock(
      const std::string& lock_file_path);
  Result<std::set<LockFile>> TryAcquireLocks(
      const std::set<std::string>& lock_file_paths);

  // Best-effort attempt to find a free instance id.
  Result<std::optional<LockFile>> TryAcquireUnusedLock();

  static Result<SharedFD> OpenLockFile(const std::string& file_path);
};

}  // namespace cvd_impl
}  // namespace cuttlefish

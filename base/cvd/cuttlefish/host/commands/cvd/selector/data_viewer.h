/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <signal.h>

#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/signals.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"

namespace cuttlefish {
namespace selector {

/**
 * Synchronizes loading and storing the instance database from and to a file.
 *
 * Guarantees atomic access to the information stored in the backing file at
 * the cost of high lock contention.
 * */
class DataViewer {
 public:
  DataViewer(const std::string& backing_file) : backing_file_(backing_file) {}

  /**
   * Provides read-only access to the data while holding a shared lock.
   *
   * This function may block until the lock can be acquired. Others can access
   * the data in read-only mode concurrently, but write access is blocked at
   * least until this function returns.
   * */
  template <typename R>
  Result<R> WithSharedLock(
      std::function<Result<R>(const cvd::PersistentData&)> task) const {
    DeadlockProtector dp(*this);
    auto fd = CF_EXPECT(LockBackingFile(LOCK_SH));
    auto data = CF_EXPECT(LoadData(fd));
    return task(data);
  }

  /**
   * Provides read-write access to the data while holding an exclusive lock.
   *
   * This function may block until the lock can be acquired. Others can't access
   * the data concurrently with this one. Any changes to the data will be
   * persisted to the file when the task functor returns successfully, no
   * changes to the backed data occur if an error is returned.
   * */
  template <typename R>
  Result<R> WithExclusiveLock(
      std::function<Result<R>(cvd::PersistentData&)> task) {
    DeadlockProtector dp(*this);
    auto fd = CF_EXPECT(LockBackingFile(LOCK_SH));
    auto data = CF_EXPECT(LoadData(fd));
    auto res = task(data);
    if (!res.ok()) {
      // Don't update if there is an error
      return res;
    }
    // Block signals while writing to the instance database file. This reduces
    // the chances of corrupting the file.
    sigset_t all_signals;
    sigfillset(&all_signals);
    SignalMasker blocker(all_signals);
    // Overwrite the file contents, don't append
    CF_EXPECTF(fd->Truncate(0) >= 0, "Failed to truncate fd: {}",
               fd->StrError());
    CF_EXPECTF(fd->LSeek(0, SEEK_SET) >= 0, "Failed to seek to 0: {}",
               fd->StrError());
    CF_EXPECT(StoreData(fd, std::move(data)));
    return res;
  }

 private:
  // Opens and locks the backing file. The lock will be dropped when the file
  // descriptor closes.
  Result<SharedFD> LockBackingFile(int op) const;

  Result<cvd::PersistentData> LoadData(SharedFD fd) const;

  Result<void> StoreData(SharedFD fd, cvd::PersistentData data);

  /**
   * Utility class to prevent deadlocks due to function reentry.
   *
   * It checks that the current thread doesn't already hold the file lock,
   * aborting the program when it detects a deadlock could occur.
   */
  class DeadlockProtector {
   public:
    DeadlockProtector(const DataViewer& dv);
    ~DeadlockProtector();
    DeadlockProtector(const DeadlockProtector&) = delete;
    DeadlockProtector(DeadlockProtector&&) = delete;

   private:
    std::mutex& mtx_;
    std::unordered_map<std::thread::id, bool>& map_;
  };
  mutable std::mutex lock_map_mtx_;
  mutable std::unordered_map<std::thread::id, bool> lock_held_by_;

  std::string backing_file_;
};

}  // namespace selector
}  // namespace cuttlefish


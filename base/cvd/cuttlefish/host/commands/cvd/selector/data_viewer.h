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

namespace cuttlefish {
namespace selector {

/**
 * Synchronizes loading and storing the instance database from and to a file.
 *
 * Guarantees "atomic" access to the information stored in the backing file at
 * the cost of high lock contention.
 * */
template <typename T>
class DataViewer {
  using SerializeFn = std::function<std::string(const T&)>;
  using DeserializeFn = std::function<Result<T>(const std::string&)>;

 public:
  DataViewer(const std::string& backing_file, SerializeFn serialize,
             DeserializeFn deserialize)
      : backing_file_(backing_file),
        serialize_(serialize),
        deserialize_(deserialize) {}

  /**
   * Provides read-only access to the data while holding a shared lock.
   *
   * This function may block until the lock can be acquired. Others can access
   * the data in read-only mode concurrently, but write access is blocked at
   * least until this function returns. It's guaranteed that the contents of the
   * backing file won't change until this function returns.
   * */
  template <typename R>
  Result<R> WithSharedLock(std::function<Result<R>(const T&)> task) const {
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
   * changes to the backing data occur if it retuns an error.
   * */
  template <typename R>
  Result<R> WithExclusiveLock(std::function<Result<R>(T&)> task) {
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
  Result<SharedFD> LockBackingFile(int op) const {
    auto fd = SharedFD::Open(backing_file_, O_CREAT | O_RDWR, 0640);
    CF_EXPECTF(fd->IsOpen(),
               "Failed to open instance database backing file: {}",
               fd->StrError());
    CF_EXPECTF(fd->Flock(op),
               "Failed to acquire lock for instance database backing file: {}",
               fd->StrError());
    return fd;
  }

  Result<T> LoadData(SharedFD fd) const {
    std::string str;
    auto read_size = ReadAll(fd, &str);
    CF_EXPECTF(read_size >= 0, "Failed to read from backing file: {}",
               fd->StrError());
    return deserialize_(str);
  }

  Result<void> StoreData(SharedFD fd, T data) {
    auto str = serialize_(data);
    auto write_size = WriteAll(fd, str);
    CF_EXPECTF(write_size == str.size(), "Failed to write to backing file: {}",
               fd->StrError());
    return {};
  }

  /**
   * Utility class to prevent deadlocks due to function reentry.
   *
   * It checks that the current thread doesn't already hold the file lock,
   * aborting the program when it detects a deadlock could occur.
   */
  class DeadlockProtector {
   public:
    DeadlockProtector(const DataViewer<T>& dv)
        : mtx_(dv.lock_map_mtx_), map_(dv.lock_held_by_) {
      std::lock_guard lock(mtx_);
      CHECK(!map_[std::this_thread::get_id()])
          << "Detected deadlock due to method reentry";
      map_[std::this_thread::get_id()] = true;
    }
    ~DeadlockProtector() {
      std::lock_guard lock(mtx_);
      map_[std::this_thread::get_id()] = false;
    }
    DeadlockProtector(const DeadlockProtector&) = delete;
    DeadlockProtector(DeadlockProtector&&) = delete;

   private:
    std::mutex& mtx_;
    std::unordered_map<std::thread::id, bool>& map_;
  };
  mutable std::mutex lock_map_mtx_;
  mutable std::unordered_map<std::thread::id, bool> lock_held_by_;

  std::string backing_file_;
  SerializeFn serialize_;
  DeserializeFn deserialize_;
};

}  // namespace selector
}  // namespace cuttlefish


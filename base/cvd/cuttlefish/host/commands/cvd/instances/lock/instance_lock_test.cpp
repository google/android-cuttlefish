/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/files/write_new_file.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class PrecreatedLocksDir {
 public:
  explicit PrecreatedLocksDir(unsigned num_locks = 16) {
    const char* tmpdir = getenv("TEST_TMPDIR");
    std::string base =
        (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    std::string tmpl = absl::StrCat(base, "/cf_lock_test.XXXXXX");
    char* created = mkdtemp(tmpl.data());
    if (created != nullptr) {
      dir_ = absl::StrCat(created, "/");
      for (unsigned i = 1; i <= num_locks; ++i) {
        std::string path = absl::StrCat(dir_, "local-instance-", i, ".lock");
        (void)WriteNewFile(path, "");
        (void)chmod(path.c_str(), 0666);
      }
      (void)chmod(dir_.c_str(), 0555);
    }
  }

  ~PrecreatedLocksDir() {
    if (!dir_.empty()) {
      (void)chmod(dir_.c_str(), 0755);
      (void)RecursivelyRemoveDirectory(dir_);
    }
  }

  const std::string& Dir() const { return dir_; }

 private:
  std::string dir_;
};

TEST(InstanceLockTest, MissingLockFileFailsWithoutCreating) {
  PrecreatedLocksDir tmp(/*num_locks=*/2);
  ASSERT_FALSE(tmp.Dir().empty());
  InstanceLockFileManager mgr(tmp.Dir(), /*create_missing_locks=*/false);

  Result<InstanceLockFile> missing = mgr.AcquireLock(99);
  EXPECT_FALSE(missing.has_value());
  EXPECT_FALSE(FileExists(absl::StrCat(tmp.Dir(), "local-instance-99.lock")));
}

TEST(InstanceLockTest,
     PrecreatedLocksWorkInReadOnlyLockDirAndResetWithoutUnlink) {
  PrecreatedLocksDir tmp(/*num_locks=*/4);
  ASSERT_FALSE(tmp.Dir().empty());
  InstanceLockFileManager mgr(tmp.Dir(), /*create_missing_locks=*/false);

  std::string lock_path = absl::StrCat(tmp.Dir(), "local-instance-1.lock");
  Result<Fd, int> before_fd = Fd::Open(lock_path, O_RDONLY);
  ASSERT_THAT(before_fd, IsOk());
  auto before_stat = before_fd->Fstat();
  ASSERT_THAT(before_stat, IsOk());

  {
    Result<InstanceLockFile> lock1 = mgr.AcquireUnusedLock();
    ASSERT_THAT(lock1, IsOk());
    EXPECT_EQ(lock1->Instance(), 1u);
    ASSERT_THAT(lock1->Status(), IsOkAndValue(InUseState::kNotInUse));
    ASSERT_THAT(lock1->Status(InUseState::kInUse), IsOk());

    // Non-blocking `TryAcquireLock` on the held file returns empty optional.
    LockFileManager raw_mgr(/*create_missing_locks=*/false);
    Result<std::optional<LockFile>> contended =
        raw_mgr.TryAcquireLock(lock_path);
    ASSERT_THAT(contended, IsOk());
    EXPECT_FALSE(contended->has_value());

    // `AcquireUnusedLock` skips held lock 1 and acquires lock 2.
    InstanceLockFileManager other_mgr(tmp.Dir(),
                                      /*create_missing_locks=*/false);
    Result<InstanceLockFile> lock2 = other_mgr.AcquireUnusedLock();
    ASSERT_THAT(lock2, IsOk());
    EXPECT_EQ(lock2->Instance(), 2u);
  }

  // After releasing `flock`, `kInUse` persists until `RemoveLockFile` resets
  // it.
  {
    Result<InstanceLockFile> next = mgr.AcquireUnusedLock();
    ASSERT_THAT(next, IsOk());
    EXPECT_EQ(next->Instance(), 2u);
  }

  ASSERT_THAT(mgr.RemoveLockFile(1), IsOk());
  EXPECT_TRUE(FileExists(lock_path));
  Result<Fd, int> after_fd = Fd::Open(lock_path, O_RDONLY);
  ASSERT_THAT(after_fd, IsOk());
  auto after_stat = after_fd->Fstat();
  ASSERT_THAT(after_stat, IsOk());
  EXPECT_EQ(before_stat->st_ino, after_stat->st_ino);

  Result<InstanceLockFile> reused = mgr.AcquireUnusedLock();
  ASSERT_THAT(reused, IsOk());
  EXPECT_EQ(reused->Instance(), 1u);
}

TEST(InstanceLockTest, AcquireUnusedLockFailsCleanlyWhenPoolExhausted) {
  PrecreatedLocksDir tmp(/*num_locks=*/2);
  ASSERT_FALSE(tmp.Dir().empty());
  InstanceLockFileManager mgr(tmp.Dir(), /*create_missing_locks=*/false);

  {
    Result<InstanceLockFile> l1 = mgr.AcquireLock(1);
    ASSERT_THAT(l1, IsOk());
    ASSERT_THAT(l1->Status(InUseState::kInUse), IsOk());
    Result<InstanceLockFile> l2 = mgr.AcquireLock(2);
    ASSERT_THAT(l2, IsOk());
    ASSERT_THAT(l2->Status(InUseState::kInUse), IsOk());
  }

  Result<InstanceLockFile> exhausted = mgr.AcquireUnusedLock();
  ASSERT_FALSE(exhausted.has_value());
  EXPECT_THAT(exhausted.error().Message(),
              testing::HasSubstr("No unused instance locks available"));
}

TEST(InstanceLockTest, ConcurrentAcquireAndResetNeverOverlap) {
  PrecreatedLocksDir tmp(/*num_locks=*/8);
  ASSERT_FALSE(tmp.Dir().empty());

  constexpr int kNumThreads = 8;
  constexpr int kIterations = 50;
  std::atomic<int> active_holders{0};
  std::atomic<bool> overlap_detected{false};

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&]() {
      InstanceLockFileManager mgr(tmp.Dir(), /*create_missing_locks=*/false);
      for (int i = 0; i < kIterations; ++i) {
        {
          Result<InstanceLockFile> lock = mgr.AcquireLock(7);
          ASSERT_THAT(lock, IsOk());
          int prev = active_holders.fetch_add(1, std::memory_order_seq_cst);
          if (prev != 0) {
            overlap_detected.store(true, std::memory_order_seq_cst);
          }
          EXPECT_THAT(lock->Status(InUseState::kInUse), IsOk());
          std::this_thread::yield();
          active_holders.fetch_sub(1, std::memory_order_seq_cst);
        }
        if ((i % 2) == 0) {
          EXPECT_THAT(mgr.RemoveLockFile(7), IsOk());
        }
      }
    });
  }
  for (std::thread& th : threads) {
    th.join();
  }

  EXPECT_FALSE(overlap_detected.load());
}

TEST(InstanceLockTest, HolderUidProtectsForeignLocksAndResetsOwnedLocks) {
  PrecreatedLocksDir tmp(/*num_locks=*/4);
  ASSERT_FALSE(tmp.Dir().empty());
  InstanceLockFileManager mgr(tmp.Dir(), /*create_missing_locks=*/false);

  {
    Result<InstanceLockFile> own = mgr.AcquireLock(1);
    ASSERT_THAT(own, IsOk());
    ASSERT_THAT(own->Status(InUseState::kInUse), IsOk());
    EXPECT_THAT(own->HolderUid(),
                IsOkAndValue(std::optional<uid_t>(geteuid())));
  }

  const uid_t foreign_uid = geteuid() + 1;
  std::string foreign_lock_path =
      absl::StrCat(tmp.Dir(), "local-instance-2.lock");
  std::string foreign_payload = absl::StrCat("I ", foreign_uid, "\n");
  Result<Fd, int> ffd = Fd::Open(foreign_lock_path, O_RDWR | O_TRUNC);
  ASSERT_THAT(ffd, IsOk());
  ASSERT_EQ(ffd->Write(foreign_payload.data(), foreign_payload.size()),
            static_cast<uint64_t>(foreign_payload.size()));

  // `RemoveLockFile` on a foreign-held lock is a no-op and preserves its state.
  ASSERT_THAT(mgr.RemoveLockFile(2), IsOk());
  {
    Result<InstanceLockFile> check2 = mgr.AcquireLock(2);
    ASSERT_THAT(check2, IsOk());
    EXPECT_THAT(check2->Status(), IsOkAndValue(InUseState::kInUse));
    EXPECT_THAT(check2->HolderUid(),
                IsOkAndValue(std::optional<uid_t>(foreign_uid)));
  }

  // `ResetAllOwnedLocks` resets lock 1 (owned by `geteuid()`) while preserving
  // lock 2 (owned by `foreign_uid`).
  ASSERT_THAT(mgr.ResetAllOwnedLocks(), IsOk());
  {
    Result<InstanceLockFile> check1 = mgr.AcquireLock(1);
    ASSERT_THAT(check1, IsOk());
    EXPECT_THAT(check1->Status(), IsOkAndValue(InUseState::kNotInUse));
    Result<InstanceLockFile> check2 = mgr.AcquireLock(2);
    ASSERT_THAT(check2, IsOk());
    EXPECT_THAT(check2->Status(), IsOkAndValue(InUseState::kInUse));
  }
}

TEST(InstanceLockTest, SandboxModeCreatesLocksOnDemand) {
  const char* tmpdir = getenv("TEST_TMPDIR");
  std::string base = (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
  std::string tmpl = absl::StrCat(base, "/cf_sandbox_lock.XXXXXX");
  char* created = mkdtemp(tmpl.data());
  ASSERT_NE(created, nullptr);
  std::string sandbox_dir = absl::StrCat(created, "/nested_locks");

  std::optional<std::string> prev_sandbox;
  if (const char* prev = getenv("CVD_IN_SANDBOX"); prev != nullptr) {
    prev_sandbox = prev;
  }
  setenv("CVD_IN_SANDBOX", "1", 1);

  InstanceLockFileManager mgr(sandbox_dir);
  Result<InstanceLockFile> lock = mgr.AcquireUnusedLock();
  if (prev_sandbox.has_value()) {
    setenv("CVD_IN_SANDBOX", prev_sandbox->c_str(), 1);
  } else {
    unsetenv("CVD_IN_SANDBOX");
  }
  ASSERT_THAT(lock, IsOk());
  EXPECT_EQ(lock->Instance(), 1u);
  (void)RecursivelyRemoveDirectory(created);
}

}  // namespace
}  // namespace cuttlefish

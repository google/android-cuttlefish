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

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/files/write_new_file.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class PrecreatedLocksDir {
 public:
  static Result<PrecreatedLocksDir> Create(const unsigned num_locks) {
    std::string tmpl = absl::StrCat(testing::TempDir(), "/cf_lock_test.XXXXXX");
    char* const created = mkdtemp(tmpl.data());
    CF_EXPECT(created != nullptr, "mkdtemp failed");
    PrecreatedLocksDir locks_dir(absl::StrCat(created, "/"));
    for (unsigned i = 1; i <= num_locks; ++i) {
      const std::string path = locks_dir.LockPath(i);
      CF_EXPECT(WriteNewFile(path, "", 0666));
    }
    CF_EXPECT_EQ(chmod(locks_dir.dir_.c_str(), 0555), 0);
    return locks_dir;
  }

  PrecreatedLocksDir(const PrecreatedLocksDir&) = delete;
  PrecreatedLocksDir& operator=(const PrecreatedLocksDir&) = delete;
  PrecreatedLocksDir(PrecreatedLocksDir&& other) noexcept
      : dir_(std::exchange(other.dir_, "")) {}
  PrecreatedLocksDir& operator=(PrecreatedLocksDir&&) = delete;
  ~PrecreatedLocksDir() { Cleanup(); }

  const std::string& Dir() const { return dir_; }
  std::string LockPath(const unsigned i) const {
    return absl::StrCat(dir_, "local-instance-", i, ".lock");
  }

 private:
  explicit PrecreatedLocksDir(std::string dir) : dir_(std::move(dir)) {}

  void Cleanup() {
    if (!dir_.empty()) {
      (void)chmod(dir_.c_str(), 0755);
      (void)RecursivelyRemoveDirectory(dir_);
    }
  }

  std::string dir_;
};

Result<void> WriteRawPayload(const std::string& path,
                             const std::string& payload) {
  Fd fd = CF_EXPECT(Fd::Open(path, O_RDWR | O_TRUNC));
  CF_EXPECT_EQ(CF_EXPECT(fd.Write(payload.data(), payload.size())),
               static_cast<uint64_t>(payload.size()));
  return {};
}

TEST(InstanceLockTest, ResetLockFileResetsInPlaceAndReusesSlot) {
  PrecreatedLocksDir tmp =
      CF_ASSERT(PrecreatedLocksDir::Create(/*num_locks=*/1));
  InstanceLockFileManager mgr(tmp.Dir());
  {
    InstanceLockFile lock = CF_ASSERT(mgr.AcquireUnusedLock());
    ASSERT_THAT(lock.Status(InUseState::kInUse), IsOk());
  }

  EXPECT_THAT(mgr.ResetLockFile(1), IsOk());
  // Resetting an already-kNotInUse lock is also a clean no-op.
  EXPECT_THAT(mgr.ResetLockFile(1), IsOk());

  EXPECT_TRUE(FileExists(tmp.LockPath(1)));
  InstanceLockFile reused = CF_ASSERT(mgr.AcquireUnusedLock());
  EXPECT_EQ(reused.Instance(), 1u);
  EXPECT_THAT(reused.HolderUid(), IsOkAndValue(std::nullopt));
}

TEST(InstanceLockTest, ResetLockFileProtectsForeignUidAndResetsLegacyLock) {
  PrecreatedLocksDir tmp =
      CF_ASSERT(PrecreatedLocksDir::Create(/*num_locks=*/2));
  const uid_t foreign_uid = geteuid() + 1;
  ASSERT_THAT(
      WriteRawPayload(tmp.LockPath(1), absl::StrCat("I ", foreign_uid, "\n")),
      IsOk());
  ASSERT_THAT(WriteRawPayload(tmp.LockPath(2), "I"), IsOk());
  InstanceLockFileManager mgr(tmp.Dir());

  EXPECT_THAT(mgr.ResetLockFile(1), IsOk());
  EXPECT_THAT(mgr.ResetLockFile(2), IsOk());

  InstanceLockFile check1 = CF_ASSERT(mgr.AcquireLock(1));
  EXPECT_THAT(check1.Status(), IsOkAndValue(InUseState::kInUse));
  EXPECT_THAT(check1.HolderUid(),
              IsOkAndValue(std::optional<uid_t>(foreign_uid)));
  InstanceLockFile check2 = CF_ASSERT(mgr.AcquireLock(2));
  EXPECT_THAT(check2.Status(), IsOkAndValue(InUseState::kNotInUse));
}

TEST(InstanceLockTest, ResetAllOwnedLocksSkipsHeldBrokenForeignAndLegacySlots) {
  PrecreatedLocksDir tmp =
      CF_ASSERT(PrecreatedLocksDir::Create(/*num_locks=*/5));
  ASSERT_EQ(chmod(tmp.Dir().c_str(), 0755), 0);
  ASSERT_EQ(unlink(tmp.LockPath(1).c_str()), 0);
  ASSERT_EQ(mkdir(tmp.LockPath(1).c_str(), 0755), 0);
  ASSERT_EQ(chmod(tmp.Dir().c_str(), 0555), 0);
  const uid_t foreign_uid = geteuid() + 1;
  ASSERT_THAT(WriteRawPayload(tmp.LockPath(2), absl::StrCat("I ", foreign_uid)),
              IsOk());
  ASSERT_THAT(WriteRawPayload(tmp.LockPath(3), "I"), IsOk());
  InstanceLockFileManager mgr(tmp.Dir());
  {
    InstanceLockFile own = CF_ASSERT(mgr.AcquireLock(4));
    ASSERT_THAT(own.Status(InUseState::kInUse), IsOk());
  }
  InstanceLockFile held = CF_ASSERT(mgr.AcquireLock(5));
  ASSERT_THAT(held.Status(InUseState::kInUse), IsOk());

  EXPECT_THAT(mgr.ResetAllOwnedLocks(), IsOk());

  EXPECT_THAT(held.Status(), IsOkAndValue(InUseState::kInUse));
  InstanceLockFile check2 = CF_ASSERT(mgr.AcquireLock(2));
  EXPECT_THAT(check2.Status(), IsOkAndValue(InUseState::kInUse));
  InstanceLockFile check3 = CF_ASSERT(mgr.AcquireLock(3));
  EXPECT_THAT(check3.Status(), IsOkAndValue(InUseState::kInUse));
  InstanceLockFile check4 = CF_ASSERT(mgr.AcquireLock(4));
  EXPECT_THAT(check4.Status(), IsOkAndValue(InUseState::kNotInUse));
}

TEST(InstanceLockTest, ResetAllOwnedLocksHandlesNumberingGaps) {
  PrecreatedLocksDir tmp =
      CF_ASSERT(PrecreatedLocksDir::Create(/*num_locks=*/3));
  ASSERT_EQ(chmod(tmp.Dir().c_str(), 0755), 0);
  ASSERT_EQ(unlink(tmp.LockPath(2).c_str()), 0);
  InstanceLockFileManager mgr(tmp.Dir());
  {
    InstanceLockFile own = CF_ASSERT(mgr.AcquireLock(3));
    ASSERT_THAT(own.Status(InUseState::kInUse), IsOk());
  }

  EXPECT_THAT(mgr.ResetAllOwnedLocks(), IsOk());

  EXPECT_FALSE(FileExists(tmp.LockPath(2)));
  InstanceLockFile check3 = CF_ASSERT(mgr.AcquireLock(3));
  EXPECT_THAT(check3.Status(), IsOkAndValue(InUseState::kNotInUse));
}

TEST(InstanceLockTest,
     ResetAllOwnedLocksIgnoresMissingDirAndNonCanonicalNames) {
  InstanceLockFileManager missing_mgr(
      absl::StrCat(testing::TempDir(), "/cf_missing_lock_dir_nonexistent/"));
  EXPECT_THAT(missing_mgr.ResetAllOwnedLocks(), IsOk());

  PrecreatedLocksDir tmp =
      CF_ASSERT(PrecreatedLocksDir::Create(/*num_locks=*/0));
  ASSERT_EQ(chmod(tmp.Dir().c_str(), 0755), 0);
  ASSERT_THAT(
      WriteNewFile(absl::StrCat(tmp.Dir(), "local-instance-0.lock"), "", 0666),
      IsOk());
  ASSERT_THAT(
      WriteNewFile(absl::StrCat(tmp.Dir(), "local-instance-01.lock"), "", 0666),
      IsOk());
  ASSERT_THAT(
      WriteNewFile(absl::StrCat(tmp.Dir(), "local-instance-+1.lock"), "", 0666),
      IsOk());
  InstanceLockFileManager mgr(tmp.Dir());

  EXPECT_THAT(mgr.ResetAllOwnedLocks(), IsOk());
  EXPECT_FALSE(FileExists(tmp.LockPath(1)));
}

TEST(InstanceLockTest,
     ConcurrentAcquireAndResetInPlacePreservesMutualExclusion) {
  constexpr unsigned kNumSlots = 16;
  constexpr int kThreads = 8;
  constexpr int kIterations = 50;
  PrecreatedLocksDir tmp =
      CF_ASSERT(PrecreatedLocksDir::Create(/*num_locks=*/kNumSlots));
  InstanceLockFileManager mgr(tmp.Dir());

  std::array<std::atomic<int>, kNumSlots + 1> active_holders{};
  std::atomic<int> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&]() {
      for (int iter = 0; iter < kIterations; ++iter) {
        unsigned id = 0;
        {
          Result<InstanceLockFile> lock = mgr.AcquireUnusedLock();
          if (!lock.has_value()) {
            failures.fetch_add(1);
            return;
          }
          id = lock->Instance();
          if (id == 0 || id > kNumSlots) {
            failures.fetch_add(1);
            return;
          }
          if (active_holders[id].fetch_add(1) != 0) {
            failures.fetch_add(1);
            return;
          }
          if (!lock->Status(InUseState::kInUse).has_value()) {
            failures.fetch_add(1);
            return;
          }
          Result<std::optional<uid_t>> holder = lock->HolderUid();
          if (!holder.has_value() || *holder != geteuid()) {
            failures.fetch_add(1);
            return;
          }
        }
        active_holders[id].fetch_sub(1);
        if (!mgr.ResetLockFile(id).has_value()) {
          failures.fetch_add(1);
          return;
        }
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  EXPECT_EQ(failures.load(), 0);
  for (unsigned i = 1; i <= kNumSlots; ++i) {
    EXPECT_TRUE(FileExists(tmp.LockPath(i)));
    InstanceLockFile check = CF_ASSERT(mgr.AcquireLock(i));
    EXPECT_THAT(check.Status(), IsOkAndValue(InUseState::kNotInUse));
  }
}

}  // namespace
}  // namespace cuttlefish

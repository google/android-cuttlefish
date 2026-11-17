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

#include "cuttlefish/host/commands/cvd/instances/data_viewer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class TempDbFile {
 public:
  TempDbFile() {
    const char* tmpdir = getenv("TEST_TMPDIR");
    std::string base =
        (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    std::string tmpl = absl::StrCat(base, "/cf_data_viewer_test.XXXXXX");
    int fd = mkstemp(tmpl.data());
    if (fd >= 0) {
      close(fd);
      path_ = tmpl;
    }
  }

  ~TempDbFile() {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
  }

  const std::string& Path() const { return path_; }

 private:
  std::string path_;
};

TEST(DataViewerTest, WithExclusiveLockAcquiresExclusiveFlock) {
  TempDbFile db_file;
  ASSERT_FALSE(db_file.Path().empty());
  DataViewer viewer(db_file.Path());

  Result<void> res = viewer.WithExclusiveLock<void>(
      [&db_file](cvd::PersistentData& data) -> Result<void> {
        // An independent file description must fail `LOCK_SH | LOCK_NB` while
        // `WithExclusiveLock` holds `LOCK_EX`.
        Fd probe = CF_EXPECT(Fd::Open(db_file.Path(), O_RDONLY | O_CLOEXEC));
        Result<void, int> probe_lock = probe.Flock(LOCK_SH | LOCK_NB);
        CF_EXPECT(!probe_lock.has_value(),
                  "Expected LOCK_SH | LOCK_NB to block against LOCK_EX");
        CF_EXPECT(*probe_lock.error() == EWOULDBLOCK ||
                  *probe_lock.error() == EAGAIN);
        data.add_instance_groups()->set_name("grp_1");
        return {};
      });
  ASSERT_THAT(res, IsOk());

  Result<int> count = viewer.WithSharedLock<int>(
      [](const cvd::PersistentData& data) -> Result<int> {
        return data.instance_groups_size();
      });
  ASSERT_THAT(count, IsOkAndValue(1));
}

TEST(DataViewerTest, ConcurrentWritersPreserveAllUpdates) {
  TempDbFile db_file;
  ASSERT_FALSE(db_file.Path().empty());

  constexpr int kNumThreads = 8;
  constexpr int kIterationsPerThread = 25;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&db_file, t]() {
      DataViewer viewer(db_file.Path());
      for (int i = 0; i < kIterationsPerThread; ++i) {
        Result<void> res = viewer.WithExclusiveLock<void>(
            [t, i](cvd::PersistentData& data) -> Result<void> {
              auto* grp = data.add_instance_groups();
              grp->set_name(absl::StrCat("grp_", t, "_", i));
              grp->set_home_directory(absl::StrCat("/tmp/home_", t, "_", i));
              return {};
            });
        EXPECT_THAT(res, IsOk());
      }
    });
  }
  for (std::thread& th : threads) {
    th.join();
  }

  DataViewer reader(db_file.Path());
  Result<int> total = reader.WithSharedLock<int>(
      [](const cvd::PersistentData& data) -> Result<int> {
        return data.instance_groups_size();
      });
  ASSERT_THAT(total, IsOkAndValue(kNumThreads * kIterationsPerThread));
}

TEST(DataViewerTest, WithConditionalExclusiveLockSkipsRewriteWhenUnmodified) {
  TempDbFile db_file;
  ASSERT_FALSE(db_file.Path().empty());
  DataViewer viewer(db_file.Path());

  ASSERT_THAT(viewer.WithExclusiveLock<void>(
                  [](cvd::PersistentData& data) -> Result<void> {
                    data.add_instance_groups()->set_name("grp_initial");
                    return {};
                  }),
              IsOk());

  // Make the backing file's mtime in the past and confirm an unmodified
  // `WithConditionalExclusiveLock` does not truncate or rewrite it.
  struct timespec past[2] = {{.tv_sec = 1000000, .tv_nsec = 0},
                             {.tv_sec = 1000000, .tv_nsec = 0}};
  ASSERT_EQ(utimensat(AT_FDCWD, db_file.Path().c_str(), past, 0), 0);

  Result<int> noop = viewer.WithConditionalExclusiveLock<int>(
      [](cvd::PersistentData& data) -> Result<std::pair<int, bool>> {
        data.add_instance_groups()->set_name("unpersisted");
        return std::make_pair(42, false);
      });
  ASSERT_THAT(noop, IsOkAndValue(42));

  struct stat st{};
  ASSERT_EQ(stat(db_file.Path().c_str(), &st), 0);
  EXPECT_EQ(st.st_mtim.tv_sec, 1000000);

  Result<int> groups_after_noop = viewer.WithSharedLock<int>(
      [](const cvd::PersistentData& data) -> Result<int> {
        return data.instance_groups_size();
      });
  ASSERT_THAT(groups_after_noop, IsOkAndValue(1));

  Result<int> modified = viewer.WithConditionalExclusiveLock<int>(
      [](cvd::PersistentData& data) -> Result<std::pair<int, bool>> {
        data.add_instance_groups()->set_name("grp_second");
        return std::make_pair(99, true);
      });
  ASSERT_THAT(modified, IsOkAndValue(99));

  Result<int> groups_after_mod = viewer.WithSharedLock<int>(
      [](const cvd::PersistentData& data) -> Result<int> {
        return data.instance_groups_size();
      });
  ASSERT_THAT(groups_after_mod, IsOkAndValue(2));
}

}  // namespace
}  // namespace cuttlefish

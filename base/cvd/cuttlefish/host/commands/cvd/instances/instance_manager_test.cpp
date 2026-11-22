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

#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"

#include <stdlib.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/files/write_new_file.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class ScopedTempDir {
 public:
  ScopedTempDir() {
    std::string tmpl = absl::StrCat(testing::TempDir(), "/cf_mgr_test.XXXXXX");
    char* created = mkdtemp(tmpl.data());
    if (created != nullptr) {
      root_ = created;
      setenv("CUTTLEFISH_METRICS_TRANSMITTER_PATH",
             absl::StrCat(root_, "/no_metrics_transmitter").c_str(), 1);
      setenv("XDG_DATA_HOME", absl::StrCat(root_, "/xdg_data").c_str(), 1);
      setenv("ANDROID_PRODUCT_OUT", root_.c_str(), 1);
    }
  }
  ScopedTempDir(const ScopedTempDir&) = delete;
  ScopedTempDir& operator=(const ScopedTempDir&) = delete;
  ~ScopedTempDir() {
    if (!root_.empty()) {
      (void)RecursivelyRemoveDirectory(root_);
    }
  }
  const std::string& Root() const { return root_; }

 private:
  std::string root_;
};

Result<LocalInstanceGroup> CreateTestGroup(InstanceManager& manager,
                                           const std::string& root,
                                           const std::string& name, unsigned id,
                                           std::string* out_slot_dir) {
  std::string slot = absl::StrCat(root, "/", name);
  std::string bin_dir = absl::StrCat(slot, "/bin");
  CF_EXPECT(EnsureDirectoryExists(bin_dir));
  CF_EXPECT(WriteNewFile(absl::StrCat(bin_dir, "/cvd_internal_start"),
                         "#!/bin/sh\nexit 0\n", 0755));
  if (out_slot_dir != nullptr) {
    *out_slot_dir = slot;
  }
  InstanceGroupParams params{
      .group_name = name,
      .instances = {InstanceParams{.instance_id = id,
                                   .per_instance_name = std::to_string(id)}},
  };
  InstanceManager::GroupDirectories dirs{
      .home = slot,
      .host_artifacts_path = slot,
      .product_out_paths = {slot},
  };
  return CF_EXPECT(
      manager.CreateInstanceGroup(std::move(params), std::move(dirs)));
}

TEST(InstanceManagerTest, PruneBrokenInstanceGroupsRemovesWipedGroupAndLock) {
  ScopedTempDir tmp;
  ASSERT_FALSE(tmp.Root().empty());
  std::string locks_dir = absl::StrCat(tmp.Root(), "/locks/");
  ASSERT_THAT(EnsureDirectoryExists(locks_dir), IsOk());
  ASSERT_THAT(
      WriteNewFile(absl::StrCat(locks_dir, "local-instance-1.lock"), "", 0666),
      IsOk());

  InstanceLockFileManager lock_manager(locks_dir);
  InstanceDatabase db(absl::StrCat(tmp.Root(), "/db.binpb"));
  InstanceManager manager(&lock_manager, db, absl::StrCat(tmp.Root(), "/cvd"));

  std::string slot_dir;
  LocalInstanceGroup group =
      CF_ASSERT(CreateTestGroup(manager, tmp.Root(), "grp1", 1, &slot_dir));
  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  ASSERT_THAT(manager.UpdateInstanceGroup(group), IsOk());

  ASSERT_THAT(RecursivelyRemoveDirectory(slot_dir), IsOk());
  ASSERT_THAT(manager.PruneBrokenInstanceGroups(), IsOk());

  EXPECT_THAT(manager.HasInstanceGroups(), IsOkAndValue(false));
  InstanceLockFile lock = CF_ASSERT(lock_manager.AcquireLock(1));
  EXPECT_THAT(lock.Status(), IsOkAndValue(InUseState::kNotInUse));
}

TEST(InstanceManagerTest, PersistInstanceStatesUsesCasAndRespectsInProgress) {
  ScopedTempDir tmp;
  ASSERT_FALSE(tmp.Root().empty());
  InstanceDatabase db(absl::StrCat(tmp.Root(), "/db.binpb"));
  InstanceManager manager(/*lock_manager=*/nullptr, db,
                          absl::StrCat(tmp.Root(), "/cvd"));

  LocalInstanceGroup group =
      CF_ASSERT(CreateTestGroup(manager, tmp.Root(), "grp1", 1, nullptr));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(manager.UpdateInstanceGroup(group), IsOk());

  // While `in_progress.lock` is held, persisting `UNREACHABLE` is skipped.
  {
    Fd in_progress =
        CF_ASSERT(InstanceManager::AcquireGroupInProgressLock(group));
    LocalInstanceGroup before = group.Clone();
    group.SetAllStates(cvd::INSTANCE_STATE_UNREACHABLE);
    ASSERT_THAT(manager.PersistInstanceStates(before, group), IsOk());

    LocalInstanceGroup stored =
        CF_ASSERT(manager.FindGroup({.group_name = "grp1"}));
    EXPECT_EQ(stored.Instances()[0].State(), cvd::INSTANCE_STATE_RUNNING);
  }

  // Without `in_progress.lock`, `RUNNING -> UNREACHABLE -> RUNNING` persists.
  LocalInstanceGroup running_before =
      CF_ASSERT(manager.FindGroup({.group_name = "grp1"}));
  LocalInstanceGroup unreachable = running_before.Clone();
  unreachable.SetAllStates(cvd::INSTANCE_STATE_UNREACHABLE);
  ASSERT_THAT(manager.PersistInstanceStates(running_before, unreachable),
              IsOk());

  LocalInstanceGroup stored_unreachable =
      CF_ASSERT(manager.FindGroup({.group_name = "grp1"}));
  EXPECT_EQ(stored_unreachable.Instances()[0].State(),
            cvd::INSTANCE_STATE_UNREACHABLE);

  LocalInstanceGroup recovered = stored_unreachable.Clone();
  recovered.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(manager.PersistInstanceStates(stored_unreachable, recovered),
              IsOk());

  LocalInstanceGroup stored_running =
      CF_ASSERT(manager.FindGroup({.group_name = "grp1"}));
  EXPECT_EQ(stored_running.Instances()[0].State(), cvd::INSTANCE_STATE_RUNNING);
}

}  // namespace
}  // namespace cuttlefish

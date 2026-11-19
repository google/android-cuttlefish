//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/cvd/instances/local_instance.h"

#include <stdlib.h>
#include <sys/un.h>

#include <chrono>
#include <optional>
#include <string>

#include "absl/strings/str_cat.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/files/write_new_file.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/data_viewer.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/metrics/device_metrics_orchestration.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace selector {
namespace {

class ScopedEnvVar {
 public:
  ScopedEnvVar(const char* name, const std::string& value) : name_(name) {
    if (const char* prev = getenv(name); prev != nullptr) {
      prev_ = prev;
    }
    setenv(name, value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (prev_.has_value()) {
      setenv(name_.c_str(), prev_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::optional<std::string> prev_;
};

class ScopedShortTempDir {
 public:
  ScopedShortTempDir() {
    std::string tmpl = "/tmp/cf_inst_test.XXXXXX";
    char* created = mkdtemp(tmpl.data());
    if (created != nullptr) {
      root_ = created;
    }
  }

  ~ScopedShortTempDir() {
    if (!root_.empty()) {
      (void)RecursivelyRemoveDirectory(root_);
    }
  }

  const std::string& Root() const { return root_; }

 private:
  std::string root_;
};

Result<LocalInstanceGroup> BuildGroupWithHome(const std::string& db_path,
                                              const std::string& name,
                                              const std::string& home_dir,
                                              unsigned id) {
  DataViewer viewer(db_path);
  CF_EXPECT(viewer.WithExclusiveLock<void>(
      [&](cvd::PersistentData& data) -> Result<void> {
        cvd::InstanceGroup* grp = data.add_instance_groups();
        grp->set_name(name);
        grp->set_home_directory(home_dir);
        grp->set_host_artifacts_path(home_dir);
        grp->set_product_out_path(home_dir);
        cvd::Instance* inst = grp->add_instances();
        inst->set_id(id);
        inst->set_name(absl::StrCat(id));
        inst->set_state(cvd::INSTANCE_STATE_RUNNING);
        return {};
      }));
  InstanceDatabase db(db_path);
  return CF_EXPECT(db.FindGroup({.group_name = name}));
}

}  // namespace

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */
TEST(LocalinstanceTest, Fields) {
  auto parent_group_res =
      LocalInstanceGroup::Builder("super").AddInstance(3, "phone").Build();
  if (!parent_group_res.has_value()) {
    /*
     * Here's why we skip the test rather than see it as a failure.
     *
     * 1. The test is specifically designed for operations in
     *    LocalInstanceRecord.
     * 2. Adding instance to a group is tested in another test suites designed
     *    for LocalInstanceGroup. It's a failure there but not here.
     *
     */
    GTEST_SKIP() << "Failed to add instance group. Set up failed.";
  }
  const auto& parent_group = *parent_group_res;
  const auto& instances = parent_group.Instances();
  auto& instance = *instances.cbegin();

  EXPECT_EQ(instance.Id(), 3);
  EXPECT_EQ(instance.Name(), "phone");
  EXPECT_EQ(parent_group.Proto().name(), "super");
}

TEST(LocalinstanceTest,
     LauncherMonitorRejectsMissingConfigAndLongFallbackPath) {
  ScopedShortTempDir tmp;
  ASSERT_FALSE(tmp.Root().empty());

  std::string long_home = absl::StrCat(
      tmp.Root(),
      "/very_long_home_directory_exceeding_unix_domain_socket_sun_path_"
      "limit_0123456789abcdef");
  std::string fallback_inst_dir =
      absl::StrCat(long_home, "/cuttlefish/instances/cvd-7");
  ASSERT_THAT(EnsureDirectoryExists(fallback_inst_dir), IsOk());

  Result<LocalInstanceGroup> group = BuildGroupWithHome(
      absl::StrCat(tmp.Root(), "/db.binpb"), "grp_long", long_home, 7);
  ASSERT_THAT(group, IsOk());

  // Missing monitor socket returns a clean error rather than aborting.
  Result<void> missing_res = group->Instances()[0].Restart(
      std::chrono::seconds(1), std::chrono::seconds(1));
  ASSERT_FALSE(missing_res.has_value());
  EXPECT_THAT(missing_res.error().Message(),
              testing::HasSubstr("does not exist"));

  // Existing monitor socket whose path exceeds `sizeof(sockaddr_un::sun_path)`
  // returns a clean error rather than hitting `CHECK_LE(namelen, 108)`.
  std::string long_sock =
      absl::StrCat(fallback_inst_dir, "/launcher_monitor.sock");
  ASSERT_GE(long_sock.size(), sizeof(sockaddr_un::sun_path));
  ASSERT_THAT(WriteNewFile(long_sock, ""), IsOk());
  Result<void> overlong_res = group->Instances()[0].Restart(
      std::chrono::seconds(1), std::chrono::seconds(1));
  ASSERT_FALSE(overlong_res.has_value());
  EXPECT_THAT(overlong_res.error().Message(),
              testing::HasSubstr("exceeds AF_UNIX limit"));
}

TEST(LocalinstanceTest, MetricsSetupRecreatesWipedDirectoryWithoutAborting) {
  ScopedShortTempDir tmp;
  ASSERT_FALSE(tmp.Root().empty());

  std::string home = absl::StrCat(tmp.Root(), "/slot_home");
  ASSERT_THAT(EnsureDirectoryExists(home), IsOk());
  ScopedEnvVar transmitter_env(
      "CUTTLEFISH_METRICS_TRANSMITTER_PATH",
      absl::StrCat(tmp.Root(), "/no_metrics_transmitter"));
  ScopedEnvVar xdg_env("XDG_DATA_HOME", absl::StrCat(tmp.Root(), "/xdg_data"));
  ScopedEnvVar product_out_env("ANDROID_PRODUCT_OUT", home);

  Result<LocalInstanceGroup> group = BuildGroupWithHome(
      absl::StrCat(tmp.Root(), "/metrics_db.binpb"), "grp_metrics", home, 1);
  ASSERT_THAT(group, IsOk());

  GatherVmInstantiationMetrics(*group);
  EXPECT_TRUE(DirectoryExists(group->MetricsDir()));

  // Simulate a caller wiping `$HOME/*` between runs: `GatherVmStartMetrics`
  // recreates `<HomeDir>/metrics` without crashing, and `GatherVmStopMetrics`
  // does not abort if `<HomeDir>/metrics` was deleted mid-run.
  ASSERT_THAT(RecursivelyRemoveDirectory(group->MetricsDir()), IsOk());
  EXPECT_FALSE(DirectoryExists(group->MetricsDir()));
  GatherVmStartMetrics(*group);
  EXPECT_TRUE(DirectoryExists(group->MetricsDir()));

  ASSERT_THAT(RecursivelyRemoveDirectory(group->MetricsDir()), IsOk());
  GatherVmStopMetrics(*group);
}

}  // namespace selector
}  // namespace cuttlefish

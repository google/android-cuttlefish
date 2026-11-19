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

#include "cuttlefish/host/commands/cvd/cli/commands/create.h"

#include <stdlib.h>
#include <sys/socket.h>  // IWYU pragma: keep
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/files/write_new_file.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/remove.h"
#include "cuttlefish/host/commands/cvd/cli/commands/start.h"
#include "cuttlefish/host/commands/cvd/cli/commands/stop.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/host/commands/cvd/instances/lock/lock_file.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::HasSubstr;

class ScopedTestWorkspace {
 public:
  ScopedTestWorkspace() {
    // Keep the root short enough to bind AF_UNIX `launcher_monitor.sock`
    // sockets (< 108 bytes); Bazel's sandbox `TEST_TMPDIR` is ~185 bytes.
    const char* test_tmpdir = getenv("TEST_TMPDIR");
    std::string base = (test_tmpdir != nullptr && test_tmpdir[0] != '\0' &&
                        std::string(test_tmpdir).size() < 50)
                           ? test_tmpdir
                           : "/tmp";
    std::string tmpl = absl::StrCat(base, "/cf_create_test.XXXXXX");
    char* created = mkdtemp(tmpl.data());
    if (created != nullptr) {
      root_ = created;
      SaveAndSetEnv("CUTTLEFISH_METRICS_TRANSMITTER_PATH",
                    absl::StrCat(root_, "/no_metrics_transmitter"));
      SaveAndSetEnv("XDG_DATA_HOME", absl::StrCat(root_, "/xdg_data"));
      SaveAndSetEnv("ANDROID_PRODUCT_OUT", root_);
    }
  }

  ~ScopedTestWorkspace() {
    for (const auto& [name, prev] : saved_env_) {
      if (prev.has_value()) {
        setenv(name.c_str(), prev->c_str(), 1);
      } else {
        unsetenv(name.c_str());
      }
    }
    if (!root_.empty()) {
      (void)RecursivelyRemoveDirectory(root_);
    }
  }

  ScopedTestWorkspace(const ScopedTestWorkspace&) = delete;
  ScopedTestWorkspace& operator=(const ScopedTestWorkspace&) = delete;

  bool Valid() const { return !root_.empty(); }
  const std::string& Root() const { return root_; }

  Result<std::string> MakeArtifactsDir(const std::string& name) const {
    std::string dir = absl::StrCat(root_, "/", name);
    std::string bin_dir = absl::StrCat(dir, "/bin");
    CF_EXPECT(EnsureDirectoryExists(bin_dir));
    CF_EXPECT(Fd::Creat(absl::StrCat(bin_dir, "/cvd_internal_start"),
                        S_IRWXU | S_IRGRP | S_IXGRP));
    return dir;
  }

 private:
  void SaveAndSetEnv(const std::string& name, const std::string& value) {
    if (const char* prev = getenv(name.c_str()); prev != nullptr) {
      saved_env_.emplace_back(name, std::string(prev));
    } else {
      saved_env_.emplace_back(name, std::nullopt);
    }
    setenv(name.c_str(), value.c_str(), 1);
  }

  std::string root_;
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_env_;
};

struct HermeticCvdContext {
  explicit HermeticCvdContext(const std::string& root)
      : locks_dir(absl::StrCat(root, "/locks/")),
        db_path(absl::StrCat(root, "/instance_database.binpb")),
        per_user_dir(absl::StrCat(root, "/per_user")),
        lock_manager(locks_dir),
        instance_db(db_path),
        instance_manager(&lock_manager, instance_db, per_user_dir) {
    (void)EnsureDirectoryExists(locks_dir);
    for (unsigned id = 1; id <= 128; ++id) {
      std::string lp = absl::StrCat(locks_dir, "local-instance-", id, ".lock");
      if (!FileExists(lp)) {
        (void)WriteNewFile(lp, "");
      }
    }
  }

  std::string locks_dir;
  std::string db_path;
  std::string per_user_dir;
  InstanceLockFileManager lock_manager;
  InstanceDatabase instance_db;
  InstanceManager instance_manager;
};

Result<void> RunCreateNoStart(
    InstanceManager& instance_manager,
    const std::unordered_map<std::string, std::string>& env,
    const std::vector<std::string>& extra_args,
    selector::SelectorOptions selectors = {}) {
  CommandRequest request =
      CF_EXPECT(CommandRequestBuilder()
                    .SetEnv(env)
                    .AddArguments({"cvd", "create", "--nostart"})
                    .AddArguments(extra_args)
                    .SetSelectorOptions(std::move(selectors))
                    .Build());
  CvdCreateCommandHandler handler(instance_manager);
  CF_EXPECT(handler.Handle(request));
  return {};
}

Result<Fd> StartMonitorSocketForInstance(const std::string& ws_root,
                                         const LocalInstance& instance) {
  std::string uds_base = absl::StrCat(ws_root, "/u");
  std::string inst_uds = absl::StrCat(uds_base, "/cvd-", instance.Id());
  CF_EXPECT(EnsureDirectoryExists(inst_uds));
  std::string config_json =
      absl::StrCat("{\"instances_uds_dir\": \"", uds_base, "\"}\n");
  CF_EXPECT(WriteNewFile(
      absl::StrCat(instance.HomeDirectory(), "/.cuttlefish_config.json"),
      config_json));
  std::string sock_path = absl::StrCat(inst_uds, "/launcher_monitor.sock");
  return CF_EXPECT(Fd::SocketLocalServer(sock_path, /*is_abstract=*/false,
                                         SOCK_STREAM, 0600));
}

TEST(CvdCreateReuseTest, ReusesStoppedGroupInSameCustomHome) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_25"));

  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  std::vector<LocalInstanceGroup> groups1 =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  ASSERT_EQ(groups1.size(), 1u);
  EXPECT_EQ(groups1[0].GroupName(), "cvd_1");
  ASSERT_EQ(groups1[0].Instances().size(), 1u);
  EXPECT_EQ(groups1[0].Instances()[0].Id(), 25u);

  ASSERT_THAT(ctx.lock_manager.ResetLockFile(25), IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());
  InstanceLockFile reused_lock = CF_ASSERT(ctx.lock_manager.AcquireLock(25));
  EXPECT_THAT(reused_lock.Status(), IsOkAndValue(InUseState::kInUse));

  std::vector<LocalInstanceGroup> groups2 =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  ASSERT_EQ(groups2.size(), 1u);
  EXPECT_EQ(groups2[0].GroupName(), "cvd_1");
  EXPECT_EQ(groups2[0].BaseDir(), groups1[0].BaseDir());
}

TEST(CvdCreateReuseTest, RefreshesStaleRunningStateLeftByStopCvd) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_25"));
  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  LocalInstanceGroup group =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group), IsOk());

  // No monitor socket or run_cvd process exists (simulating stop_cvd teardown).
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  LocalInstanceGroup refreshed =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  ASSERT_EQ(refreshed.Instances().size(), 1u);
  EXPECT_EQ(refreshed.Instances()[0].State(), cvd::INSTANCE_STATE_STOPPED);
}

TEST(CvdCreateReuseTest, RejectsReuseWhenLauncherMonitorIsAlive) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_10"));
  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=10"}),
              IsOk());

  LocalInstanceGroup group =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group), IsOk());

  Fd monitor_server =
      CF_ASSERT(StartMonitorSocketForInstance(ws.Root(), group.Instances()[0]));

  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=10"}),
              IsErrorAndMessage(HasSubstr("Group is already running")));
}

TEST(CvdCreateReuseTest, RejectsReuseWhenGroupStartIsInProgress) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_11"));
  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=11"}),
              IsOk());

  LocalInstanceGroup group =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  Fd in_progress =
      CF_ASSERT(InstanceManager::AcquireGroupInProgressLock(group));
  group.SetAllStates(cvd::INSTANCE_STATE_STARTING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group), IsOk());

  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=11"}),
              IsErrorAndMessage(HasSubstr("Group is already running")));
}

TEST(CvdCreateReuseTest, AllowsMultipleConcurrentSlotsUnderSameUser) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot17 = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_17"));
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot17}},
                               {"--reuse=true", "--base_instance_num=17"}),
              IsOk());

  LocalInstanceGroup group17 =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  group17.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group17), IsOk());
  Fd monitor17 = CF_ASSERT(
      StartMonitorSocketForInstance(ws.Root(), group17.Instances()[0]));

  const std::vector<int> shards = {44, 58, 1, 4, 45, 33, 56, 5};
  for (int shard : shards) {
    SCOPED_TRACE(shard);
    std::string slot =
        CF_ASSERT(ws.MakeArtifactsDir(absl::StrCat("acloud_cf_", shard)));
    ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot}},
                                 {"--reuse=true",
                                  absl::StrCat("--base_instance_num=", shard)}),
                IsOk());
  }

  std::vector<LocalInstanceGroup> all_groups =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  EXPECT_EQ(all_groups.size(), 9u);
}

TEST(CvdCreateReuseTest, PrunesDeletedGroupDirectoryAndRecyclesInstanceId) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string temp1 = CF_ASSERT(ws.MakeArtifactsDir("temp_run_1"));
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", temp1}},
                               {"--reuse=true"}),
              IsOk());

  LocalInstanceGroup group1 =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  EXPECT_EQ(group1.Instances()[0].Id(), 1u);
  // Simulate stop_cvd leaving state RUNNING followed by temp dir removal.
  group1.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group1), IsOk());
  ASSERT_THAT(RecursivelyRemoveDirectory(temp1), IsOk());

  std::string temp2 = CF_ASSERT(ws.MakeArtifactsDir("temp_run_2"));
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", temp2}},
                               {"--reuse=true"}),
              IsOk());

  std::vector<LocalInstanceGroup> groups =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].Instances()[0].Id(), 1u);
}

TEST(CvdCreateReuseTest, ReplacesStoppedSlotGroupWhenProductOrCountChanges) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("dev_home"));
  std::string prod_a = CF_ASSERT(ws.MakeArtifactsDir("prod_a"));
  std::string prod_b = CF_ASSERT(ws.MakeArtifactsDir("prod_b"));

  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};
  ASSERT_THAT(
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + slot,
                        "--product_path=" + prod_a, "--num_instances=2"}),
      IsOk());

  // Invalid product_path must fail BEFORE removing the existing stopped group.
  EXPECT_THAT(
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + slot,
                        "--product_path=" + ws.Root() + "/missing_dir"}),
      IsError());
  std::vector<LocalInstanceGroup> kept_groups =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  ASSERT_EQ(kept_groups.size(), 1u);
  EXPECT_EQ(kept_groups[0].Instances().size(), 2u);

  // Explicit --group_name selector with mismatched product_path must fail.
  EXPECT_THAT(
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + slot,
                        "--product_path=" + prod_b, "--num_instances=2"},
                       {.group_name = "cvd_1"}),
      IsErrorAndMessage(HasSubstr("Trying to change the host paths")));

  // Slot-based reuse (no selector) replaces the stopped group without leaking.
  ASSERT_THAT(
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + slot,
                        "--product_path=" + prod_b, "--num_instances=1"}),
      IsOk());
  std::vector<LocalInstanceGroup> replaced_groups =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  ASSERT_EQ(replaced_groups.size(), 1u);
  EXPECT_EQ(replaced_groups[0].Instances().size(), 1u);
  EXPECT_EQ(replaced_groups[0].Instances()[0].Id(), 1u);
}

TEST(CvdCreateReuseTest, FailsWhenMultipleDefaultHomeGroupsAreAmbiguous) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string sys_home = CF_ASSERT(SystemWideUserHome());
  std::string artifacts = CF_ASSERT(ws.MakeArtifactsDir("artifacts"));

  std::unordered_map<std::string, std::string> env = {
      {"HOME", sys_home},
      {"ANDROID_HOST_OUT", artifacts},
      {"ANDROID_PRODUCT_OUT", artifacts},
  };
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env, {"--reuse=false"}),
              IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env, {"--reuse=false"}),
              IsOk());

  // Bare --reuse=true with two default-HOME groups is ambiguous.
  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env, {"--reuse=true"}),
              IsErrorAndMessage(HasSubstr("Unclear which group to reuse")));

  // Explicit --base_instance_num=2 disambiguates cleanly!
  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=2"}),
              IsOk());
}

TEST(CvdCreateReuseTest, ConcurrentMultiProcessCreatesPreserveAllGroups) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext setup_ctx(ws.Root());

  constexpr int kNumProcs = 12;
  std::vector<std::string> slot_dirs;
  slot_dirs.reserve(kNumProcs);
  for (int i = 1; i <= kNumProcs; ++i) {
    slot_dirs.push_back(
        CF_ASSERT(ws.MakeArtifactsDir(absl::StrCat("concurrent_slot_", i))));
  }

  std::vector<pid_t> pids;
  pids.reserve(kNumProcs);
  for (int i = 1; i <= kNumProcs; ++i) {
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
      HermeticCvdContext child_ctx(ws.Root());
      Result<void> res = RunCreateNoStart(
          child_ctx.instance_manager, {{"HOME", slot_dirs[i - 1]}},
          {"--reuse=true", absl::StrCat("--base_instance_num=", i)});
      if (!res.has_value()) {
        std::cerr << "Child " << i << " failed: " << res.error().FormatForEnv()
                  << std::endl;
      }
      _exit(res.has_value() ? 0 : 1);
    }
    pids.push_back(pid);
  }

  for (pid_t pid : pids) {
    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0)
        << "Child pid " << pid << " failed with status " << status;
  }

  HermeticCvdContext verify_ctx(ws.Root());
  std::vector<LocalInstanceGroup> groups =
      CF_ASSERT(verify_ctx.instance_manager.FindGroups({}));
  EXPECT_EQ(groups.size(), static_cast<size_t>(kNumProcs));
}

TEST(CvdCreateReuseTest,
     HandlesLongGroupPathsWithoutAbortingWhenMonitorMissing) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  std::string deep_root =
      absl::StrCat(ws.Root(),
                   "/very_long_per_user_directory_name_exceeding_unix_socket_"
                   "limit_0123456789");
  ASSERT_THAT(EnsureDirectoryExists(deep_root), IsOk());
  HermeticCvdContext ctx(deep_root);

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_99"));
  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=99"}),
              IsOk());

  LocalInstanceGroup group =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  std::string fallback_inst_dir =
      absl::StrCat(group.HomeDir(), "/cuttlefish/instances/cvd-99");
  ASSERT_THAT(EnsureDirectoryExists(fallback_inst_dir), IsOk());
  std::string long_monitor_path =
      absl::StrCat(fallback_inst_dir, "/launcher_monitor.sock");
  ASSERT_GE(long_monitor_path.size(), sizeof(sockaddr_un::sun_path));
  ASSERT_THAT(WriteNewFile(long_monitor_path, ""), IsOk());
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group), IsOk());

  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=99"}),
              IsOk());
}

TEST(CvdCreateReuseTest,
     StandaloneCvdStartRefreshesStaleRunningStateAndRestoresBootFailedLock) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_25"));

  std::unordered_map<std::string, std::string> env = {
      {"HOME", slot},
      {"ANDROID_HOST_OUT", slot},
      {"ANDROID_PRODUCT_OUT", slot},
  };
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  // Simulate `stop_cvd` leaving stale RUNNING state AND `CvdResetGroup` having
  // removed `local-instance-25.lock`.
  LocalInstanceGroup group =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group), IsOk());
  ASSERT_THAT(ctx.lock_manager.ResetLockFile(25), IsOk());

  CommandRequest start_req =
      CF_ASSERT(CommandRequestBuilder()
                    .SetEnv(env)
                    .AddArguments({"cvd", "start", "--daemon=true",
                                   "--print_group_format=none"})
                    .SetSelectorOptions({.group_name = "cvd_1"})
                    .Build());
  CvdStartCommandHandler start_handler(ctx.instance_manager);
  // In the unit test binary (`create_test` rather than
  // `cuttlefish-common/bin/cvd`), `Handle` must succeed through
  // `AcquireGroupInProgressLock`,
  // `RefreshInstanceGroupState(caller_holds_in_progress_lock=true)` (clearing
  // stale RUNNING -> STOPPED instead of self-deadlocking on
  // `in_progress.lock`), and `EnsureInstanceLocksInUse`, before
  // `HostPackageSubstitution` checks the executable path.
  EXPECT_THAT(start_handler.Handle(start_req),
              IsErrorAndMessage(HasSubstr("cuttlefish-common/bin")));

  LocalInstanceGroup refreshed =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  EXPECT_EQ(refreshed.Instances()[0].State(), cvd::INSTANCE_STATE_STOPPED);

  InstanceLockFile lock = CF_ASSERT(ctx.lock_manager.AcquireLock(25));
  EXPECT_THAT(lock.Status(), IsOkAndValue(InUseState::kInUse));
}

TEST(CvdCreateReuseTest, StandaloneCvdStopAndRemoveHandleStaleRunningState) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_30"));
  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=30"}),
              IsOk());

  LocalInstanceGroup group =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  group.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group), IsOk());

  // `cvd stop` refreshes stale RUNNING -> STOPPED and reports not running.
  CommandRequest stop_req =
      CF_ASSERT(CommandRequestBuilder()
                    .SetEnv(env)
                    .AddArguments({"cvd", "stop"})
                    .SetSelectorOptions({.group_name = "cvd_1"})
                    .Build());
  CvdStopCommandHandler stop_handler(ctx.instance_manager);
  EXPECT_THAT(stop_handler.Handle(stop_req),
              IsErrorAndMessage(HasSubstr("Selected group is not running")));

  LocalInstanceGroup after_stop =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  EXPECT_EQ(after_stop.Instances()[0].State(), cvd::INSTANCE_STATE_STOPPED);

  // Mark stale RUNNING again and verify `cvd rm` removes it cleanly without
  // needing a working `stop_cvd` binary.
  after_stop.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(after_stop), IsOk());

  CommandRequest rm_req =
      CF_ASSERT(CommandRequestBuilder()
                    .SetEnv(env)
                    .AddArguments({"cvd", "rm"})
                    .SetSelectorOptions({.group_name = "cvd_1"})
                    .Build());
  RemoveCvdCommandHandler rm_handler(ctx.instance_manager);
  ASSERT_THAT(rm_handler.Handle(rm_req), IsOk());

  std::vector<LocalInstanceGroup> remaining =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  EXPECT_TRUE(remaining.empty());
  EXPECT_FALSE(
      DirectoryExists(after_stop.BaseDir(), /*follow_symlinks=*/false));
}

TEST(CvdCreateReuseTest,
     PreparingGroupHoldingInProgressLockSurvivesConcurrentPrune) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  // Create an empty fetch target dir (no `bin/cvd_internal_start` yet),
  // simulating `cvd load` while `cvd fetch` is in progress.
  std::string fetching_dir = absl::StrCat(ws.Root(), "/fetching_artifacts");
  ASSERT_THAT(EnsureDirectoryExists(fetching_dir), IsOk());

  std::optional<Fd> in_progress_lock;
  LocalInstanceGroup preparing_group =
      CF_ASSERT(ctx.instance_manager.CreateInstanceGroup(
          InstanceGroupParams{
              .group_name = "loading_grp",
              .instances = {InstanceParams{.instance_id = 50,
                                           .per_instance_name = "ins-1"}},
          },
          InstanceManager::GroupDirectories{
              .home = fetching_dir,
              .host_artifacts_path = fetching_dir,
              .product_out_paths = {fetching_dir},
          },
          &in_progress_lock));
  ASSERT_TRUE(in_progress_lock.has_value());

  // Concurrent `cvd create --reuse=true` on another slot triggers
  // `PruneBrokenInstanceGroups()`, which must NOT prune `loading_grp` while
  // `in_progress_lock` is held.
  std::string slot2 = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_51"));
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot2}},
                               {"--reuse=true", "--base_instance_num=51"}),
              IsOk());

  LocalInstanceGroup still_preparing =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "loading_grp"}));
  EXPECT_EQ(still_preparing.Instances()[0].State(),
            cvd::INSTANCE_STATE_PREPARING);
  EXPECT_TRUE(DirectoryExists(still_preparing.BaseDir()));

  // Once `in_progress_lock` is dropped (e.g. `cvd load` was SIGKILLed mid-fetch
  // leaving empty artifacts), `PruneBrokenInstanceGroups` cleans it up.
  in_progress_lock.reset();
  ASSERT_THAT(ctx.instance_manager.PruneBrokenInstanceGroups(), IsOk());
  std::vector<LocalInstanceGroup> after_prune =
      CF_ASSERT(ctx.instance_manager.FindGroups({.group_name = "loading_grp"}));
  EXPECT_TRUE(after_prune.empty());
}

TEST(CvdCreateReuseTest,
     DefaultReuseFalsePrunesBrokenStaleGroupsAndRecyclesInstanceId) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string temp1 = CF_ASSERT(ws.MakeArtifactsDir("ephemeral_1"));
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", temp1}},
                               {"--reuse=false"}),
              IsOk());

  LocalInstanceGroup group1 =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  EXPECT_EQ(group1.Instances()[0].Id(), 1u);
  group1.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(group1), IsOk());
  ASSERT_THAT(RecursivelyRemoveDirectory(temp1), IsOk());

  std::string temp2 = CF_ASSERT(ws.MakeArtifactsDir("ephemeral_2"));
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", temp2}},
                               {"--reuse=false"}),
              IsOk());

  std::vector<LocalInstanceGroup> groups =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].Instances()[0].Id(), 1u);
}

TEST(CvdCreateReuseTest, MultiSlotPerInstanceNameIsolationAndMismatch) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot1 = CF_ASSERT(ws.MakeArtifactsDir("slot_phone_1"));
  std::string slot2 = CF_ASSERT(ws.MakeArtifactsDir("slot_phone_2"));

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot1}},
                               {"--reuse=true", "--base_instance_num=1"},
                               {.instance_names = {{"phone"}}}),
              IsOk());
  LocalInstanceGroup grp1 =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  grp1.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(grp1), IsOk());
  Fd mon1 =
      CF_ASSERT(StartMonitorSocketForInstance(ws.Root(), grp1.Instances()[0]));

  // Second slot with the SAME `--instance_name=phone` creates its own group.
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot2}},
                               {"--reuse=true", "--base_instance_num=2"},
                               {.instance_names = {{"phone"}}}),
              IsOk());
  std::vector<LocalInstanceGroup> groups =
      CF_ASSERT(ctx.instance_manager.FindGroups({}));
  EXPECT_EQ(groups.size(), 2u);

  // Explicit `--group_name=cvd_2` with mismatched `--instance_name=tablet`
  // fails.
  EXPECT_THAT(
      RunCreateNoStart(ctx.instance_manager, {{"HOME", slot2}},
                       {"--reuse=true", "--base_instance_num=2"},
                       {.group_name = "cvd_2", .instance_names = {{"tablet"}}}),
      IsErrorAndMessage(HasSubstr("Trying to change the instances")));
}

TEST(CvdCreateReuseTest,
     UnrelatedActiveGroupWithDeletedCustomHomeDoesNotBreakOtherSlots) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot17 = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_17"));
  std::string slot44 = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_44"));

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot17}},
                               {"--reuse=true", "--base_instance_num=17"}),
              IsOk());
  LocalInstanceGroup grp17 =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  // Hold `in_progress.lock` so `grp17` stays active even after its custom HOME
  // directory is deleted on disk (creating a dangling `<BaseDir>/home`
  // symlink).
  Fd lock17 = CF_ASSERT(InstanceManager::AcquireGroupInProgressLock(grp17));
  grp17.SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(grp17), IsOk());
  ASSERT_THAT(RecursivelyRemoveDirectory(slot17), IsOk());

  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot44}},
                               {"--reuse=true", "--base_instance_num=44"}),
              IsOk());
}

TEST(CvdCreateReuseTest,
     DefaultHomeStoppedGroupPreservesUserdataOnPathMismatch) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string sys_home = CF_ASSERT(SystemWideUserHome());
  std::string prod_a = CF_ASSERT(ws.MakeArtifactsDir("default_prod_a"));
  std::string prod_b = CF_ASSERT(ws.MakeArtifactsDir("default_prod_b"));

  std::unordered_map<std::string, std::string> env = {{"HOME", sys_home}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--host_path=" + prod_a,
                                "--product_path=" + prod_a}),
              IsOk());

  LocalInstanceGroup grp =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  std::string userdata_sentinel = absl::StrCat(grp.HomeDir(), "/userdata.img");
  ASSERT_THAT(WriteNewFile(userdata_sentinel, "precious-userdata"), IsOk());

  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--host_path=" + prod_a,
                                "--product_path=" + prod_b}),
              IsErrorAndMessage(HasSubstr("Trying to change the host paths")));
  EXPECT_TRUE(FileExists(userdata_sentinel));
}

TEST(CvdCreateReuseTest, DuplicateCustomHomeAcrossGroupsIsRejectedByDatabase) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("shared_slot"));
  std::string slot2 = CF_ASSERT(ws.MakeArtifactsDir("other_slot"));
  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=false", "--base_instance_num=1"}),
              IsOk());
  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=false", "--base_instance_num=2"}),
              IsErrorAndMessage(
                  HasSubstr("An instance group already exists with HOME")));
  // Newly allocated lock for failed group (`2`) must be rolled back to
  // `kNotInUse`!
  InstanceLockFile lock2 = CF_ASSERT(ctx.lock_manager.AcquireLock(2));
  EXPECT_THAT(lock2.Status(), IsOkAndValue(InUseState::kNotInUse));

  // Same-user duplicate instance ID (`1`) must fail without releasing group 1's
  // existing `kInUse` lock!
  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", slot2}},
                               {"--reuse=false", "--base_instance_num=1"}),
              IsError());
  EXPECT_TRUE(FileExists(absl::StrCat(ctx.locks_dir, "local-instance-1.lock")));
  InstanceLockFile lock1 = CF_ASSERT(ctx.lock_manager.AcquireLock(1));
  EXPECT_THAT(lock1.Status(), IsOkAndValue(InUseState::kInUse));
}

TEST(CvdCreateReuseTest, RemoveCvdRejectsGroupWhenTransitionIsInProgress) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  std::string slot = CF_ASSERT(ws.MakeArtifactsDir("acloud_cf_60"));
  std::unordered_map<std::string, std::string> env = {{"HOME", slot}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=60"}),
              IsOk());

  LocalInstanceGroup group =
      CF_ASSERT(ctx.instance_manager.FindGroup({.group_name = "cvd_1"}));
  Fd in_progress =
      CF_ASSERT(InstanceManager::AcquireGroupInProgressLock(group));

  CommandRequest rm_req =
      CF_ASSERT(CommandRequestBuilder()
                    .SetEnv(env)
                    .AddArguments({"cvd", "rm"})
                    .SetSelectorOptions({.group_name = "cvd_1"})
                    .Build());
  RemoveCvdCommandHandler rm_handler(ctx.instance_manager);
  EXPECT_THAT(rm_handler.Handle(rm_req),
              IsErrorAndMessage(
                  HasSubstr("Group transition is currently in progress")));
}

}  // namespace
}  // namespace cuttlefish

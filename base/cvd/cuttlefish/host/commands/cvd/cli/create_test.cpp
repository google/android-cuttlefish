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
#include "cuttlefish/common/libs/fs/shared_fd.h"
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
        instance_manager(lock_manager, instance_db, per_user_dir) {
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

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_25");
  ASSERT_THAT(slot, IsOk());

  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  Result<std::vector<LocalInstanceGroup>> groups1 =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(groups1, IsOk());
  ASSERT_EQ(groups1->size(), 1u);
  EXPECT_EQ((*groups1)[0].GroupName(), "cvd_1");
  ASSERT_EQ((*groups1)[0].Instances().size(), 1u);
  EXPECT_EQ((*groups1)[0].Instances()[0].Id(), 25u);

  ASSERT_THAT(ctx.lock_manager.RemoveLockFile(25), IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());
  Result<InstanceLockFile> reused_lock = ctx.lock_manager.AcquireLock(25);
  ASSERT_THAT(reused_lock, IsOk());
  EXPECT_THAT(reused_lock->Status(), IsOkAndValue(InUseState::kInUse));

  Result<std::vector<LocalInstanceGroup>> groups2 =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(groups2, IsOk());
  ASSERT_EQ(groups2->size(), 1u);
  EXPECT_EQ((*groups2)[0].GroupName(), "cvd_1");
  EXPECT_EQ((*groups2)[0].BaseDir(), (*groups1)[0].BaseDir());
}

TEST(CvdCreateReuseTest, RefreshesStaleRunningStateLeftByStopCvd) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_25");
  ASSERT_THAT(slot, IsOk());
  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  Result<LocalInstanceGroup> group =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group, IsOk());
  group->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group), IsOk());

  // No monitor socket or run_cvd process exists (simulating stop_cvd teardown).
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  Result<LocalInstanceGroup> refreshed =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(refreshed, IsOk());
  ASSERT_EQ(refreshed->Instances().size(), 1u);
  EXPECT_EQ(refreshed->Instances()[0].State(), cvd::INSTANCE_STATE_STOPPED);
}

TEST(CvdCreateReuseTest, RejectsReuseWhenLauncherMonitorIsAlive) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_10");
  ASSERT_THAT(slot, IsOk());
  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=10"}),
              IsOk());

  Result<LocalInstanceGroup> group =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group, IsOk());
  group->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group), IsOk());

  Result<Fd> monitor_server =
      StartMonitorSocketForInstance(ws.Root(), group->Instances()[0]);
  ASSERT_THAT(monitor_server, IsOk());

  Result<void> second_res = RunCreateNoStart(
      ctx.instance_manager, env, {"--reuse=true", "--base_instance_num=10"});
  ASSERT_FALSE(second_res.has_value());
  EXPECT_THAT(second_res.error().Message(),
              HasSubstr("Group is already running"));
}

TEST(CvdCreateReuseTest, RejectsReuseWhenGroupStartIsInProgress) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_11");
  ASSERT_THAT(slot, IsOk());
  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=11"}),
              IsOk());

  Result<LocalInstanceGroup> group =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group, IsOk());
  Result<SharedFD> in_progress =
      InstanceManager::AcquireGroupInProgressLock(*group);
  ASSERT_THAT(in_progress, IsOk());
  group->SetAllStates(cvd::INSTANCE_STATE_STARTING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group), IsOk());

  Result<void> second_res = RunCreateNoStart(
      ctx.instance_manager, env, {"--reuse=true", "--base_instance_num=11"});
  ASSERT_FALSE(second_res.has_value());
  EXPECT_THAT(second_res.error().Message(),
              HasSubstr("Group is already running"));
}

TEST(CvdCreateReuseTest, AllowsMultipleConcurrentSlotsUnderSameUser) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot17 = ws.MakeArtifactsDir("acloud_cf_17");
  ASSERT_THAT(slot17, IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot17}},
                               {"--reuse=true", "--base_instance_num=17"}),
              IsOk());

  Result<LocalInstanceGroup> group17 =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group17, IsOk());
  group17->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group17), IsOk());
  Result<Fd> monitor17 =
      StartMonitorSocketForInstance(ws.Root(), group17->Instances()[0]);
  ASSERT_THAT(monitor17, IsOk());

  const std::vector<int> shards = {44, 58, 1, 4, 45, 33, 56, 5};
  for (int shard : shards) {
    SCOPED_TRACE(shard);
    Result<std::string> slot =
        ws.MakeArtifactsDir(absl::StrCat("acloud_cf_", shard));
    ASSERT_THAT(slot, IsOk());
    ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot}},
                                 {"--reuse=true",
                                  absl::StrCat("--base_instance_num=", shard)}),
                IsOk());
  }

  Result<std::vector<LocalInstanceGroup>> all_groups =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(all_groups, IsOk());
  EXPECT_EQ(all_groups->size(), 9u);
}

TEST(CvdCreateReuseTest, PrunesDeletedGroupDirectoryAndRecyclesInstanceId) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> temp1 = ws.MakeArtifactsDir("temp_run_1");
  ASSERT_THAT(temp1, IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *temp1}},
                               {"--reuse=true"}),
              IsOk());

  Result<LocalInstanceGroup> group1 =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group1, IsOk());
  EXPECT_EQ(group1->Instances()[0].Id(), 1u);
  // Simulate stop_cvd leaving state RUNNING followed by temp dir removal.
  group1->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group1), IsOk());
  ASSERT_THAT(RecursivelyRemoveDirectory(*temp1), IsOk());

  Result<std::string> temp2 = ws.MakeArtifactsDir("temp_run_2");
  ASSERT_THAT(temp2, IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *temp2}},
                               {"--reuse=true"}),
              IsOk());

  Result<std::vector<LocalInstanceGroup>> groups =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(groups, IsOk());
  ASSERT_EQ(groups->size(), 1u);
  EXPECT_EQ((*groups)[0].Instances()[0].Id(), 1u);
}

TEST(CvdCreateReuseTest, ReplacesStoppedSlotGroupWhenProductOrCountChanges) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("dev_home");
  Result<std::string> prod_a = ws.MakeArtifactsDir("prod_a");
  Result<std::string> prod_b = ws.MakeArtifactsDir("prod_b");
  ASSERT_THAT(slot, IsOk());
  ASSERT_THAT(prod_a, IsOk());
  ASSERT_THAT(prod_b, IsOk());

  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};
  ASSERT_THAT(
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + *slot,
                        "--product_path=" + *prod_a, "--num_instances=2"}),
      IsOk());

  // Invalid product_path must fail BEFORE removing the existing stopped group.
  ASSERT_FALSE(
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + *slot,
                        "--product_path=" + ws.Root() + "/missing_dir"})
          .has_value());
  Result<std::vector<LocalInstanceGroup>> kept_groups =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(kept_groups, IsOk());
  ASSERT_EQ(kept_groups->size(), 1u);
  EXPECT_EQ((*kept_groups)[0].Instances().size(), 2u);

  // Explicit --group_name selector with mismatched product_path must fail.
  Result<void> selector_mismatch =
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + *slot,
                        "--product_path=" + *prod_b, "--num_instances=2"},
                       {.group_name = "cvd_1"});
  ASSERT_FALSE(selector_mismatch.has_value());
  EXPECT_THAT(selector_mismatch.error().Message(),
              HasSubstr("Trying to change the host paths"));

  // Slot-based reuse (no selector) replaces the stopped group without leaking.
  ASSERT_THAT(
      RunCreateNoStart(ctx.instance_manager, env,
                       {"--reuse=true", "--host_path=" + *slot,
                        "--product_path=" + *prod_b, "--num_instances=1"}),
      IsOk());
  Result<std::vector<LocalInstanceGroup>> replaced_groups =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(replaced_groups, IsOk());
  ASSERT_EQ(replaced_groups->size(), 1u);
  EXPECT_EQ((*replaced_groups)[0].Instances().size(), 1u);
  EXPECT_EQ((*replaced_groups)[0].Instances()[0].Id(), 1u);
}

TEST(CvdCreateReuseTest, FailsWhenMultipleDefaultHomeGroupsAreAmbiguous) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> sys_home = SystemWideUserHome();
  ASSERT_THAT(sys_home, IsOk());
  Result<std::string> artifacts = ws.MakeArtifactsDir("artifacts");
  ASSERT_THAT(artifacts, IsOk());

  std::unordered_map<std::string, std::string> env = {
      {"HOME", *sys_home},
      {"ANDROID_HOST_OUT", *artifacts},
      {"ANDROID_PRODUCT_OUT", *artifacts},
  };
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env, {"--reuse=false"}),
              IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env, {"--reuse=false"}),
              IsOk());

  // Bare --reuse=true with two default-HOME groups is ambiguous.
  Result<void> ambiguous =
      RunCreateNoStart(ctx.instance_manager, env, {"--reuse=true"});
  ASSERT_FALSE(ambiguous.has_value());
  EXPECT_THAT(ambiguous.error().Message(),
              HasSubstr("Unclear which group to reuse"));

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
    Result<std::string> slot =
        ws.MakeArtifactsDir(absl::StrCat("concurrent_slot_", i));
    ASSERT_THAT(slot, IsOk());
    slot_dirs.push_back(*slot);
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
  Result<std::vector<LocalInstanceGroup>> groups =
      verify_ctx.instance_manager.FindGroups({});
  ASSERT_THAT(groups, IsOk());
  EXPECT_EQ(groups->size(), static_cast<size_t>(kNumProcs));
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

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_99");
  ASSERT_THAT(slot, IsOk());
  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=99"}),
              IsOk());

  Result<LocalInstanceGroup> group =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group, IsOk());
  std::string fallback_inst_dir =
      absl::StrCat(group->HomeDir(), "/cuttlefish/instances/cvd-99");
  ASSERT_THAT(EnsureDirectoryExists(fallback_inst_dir), IsOk());
  std::string long_monitor_path =
      absl::StrCat(fallback_inst_dir, "/launcher_monitor.sock");
  ASSERT_GE(long_monitor_path.size(), sizeof(sockaddr_un::sun_path));
  ASSERT_THAT(WriteNewFile(long_monitor_path, ""), IsOk());
  group->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group), IsOk());

  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=99"}),
              IsOk());
}

TEST(CvdCreateReuseTest,
     StandaloneCvdStartRefreshesStaleRunningStateAndRestoresBootFailedLock) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_25");
  ASSERT_THAT(slot, IsOk());

  std::unordered_map<std::string, std::string> env = {
      {"HOME", *slot},
      {"ANDROID_HOST_OUT", *slot},
      {"ANDROID_PRODUCT_OUT", *slot},
  };
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=25"}),
              IsOk());

  // Simulate `stop_cvd` leaving stale RUNNING state AND `CvdResetGroup` having
  // removed `local-instance-25.lock`.
  Result<LocalInstanceGroup> group =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group, IsOk());
  group->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group), IsOk());
  ASSERT_THAT(ctx.lock_manager.RemoveLockFile(25), IsOk());

  Result<CommandRequest> start_req =
      CommandRequestBuilder()
          .SetEnv(env)
          .AddArguments(
              {"cvd", "start", "--daemon=true", "--print_group_format=none"})
          .SetSelectorOptions({.group_name = "cvd_1"})
          .Build();
  ASSERT_THAT(start_req, IsOk());
  CvdStartCommandHandler start_handler(ctx.instance_manager);
  // In the unit test binary (`create_test` rather than
  // `cuttlefish-common/bin/cvd`), `Handle` must succeed through
  // `AcquireGroupInProgressLock`,
  // `RefreshInstanceGroupState(caller_holds_in_progress_lock=true)` (clearing
  // stale RUNNING -> STOPPED instead of self-deadlocking on
  // `in_progress.lock`), and `EnsureInstanceLocksInUse`, before
  // `HostPackageSubstitution` checks the executable path.
  Result<void> start_res = start_handler.Handle(*start_req);
  ASSERT_FALSE(start_res.has_value());
  EXPECT_THAT(start_res.error().Message(), HasSubstr("cuttlefish-common/bin"));

  Result<LocalInstanceGroup> refreshed =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(refreshed, IsOk());
  EXPECT_EQ(refreshed->Instances()[0].State(), cvd::INSTANCE_STATE_STOPPED);

  Result<InstanceLockFile> lock = ctx.lock_manager.AcquireLock(25);
  ASSERT_THAT(lock, IsOk());
  Result<InUseState> lock_state = lock->Status();
  ASSERT_THAT(lock_state, IsOk());
  EXPECT_EQ(*lock_state, InUseState::kInUse);
}

TEST(CvdCreateReuseTest, StandaloneCvdStopAndRemoveHandleStaleRunningState) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_30");
  ASSERT_THAT(slot, IsOk());
  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=30"}),
              IsOk());

  Result<LocalInstanceGroup> group =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group, IsOk());
  group->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group), IsOk());

  // `cvd stop` refreshes stale RUNNING -> STOPPED and reports not running.
  Result<CommandRequest> stop_req =
      CommandRequestBuilder()
          .SetEnv(env)
          .AddArguments({"cvd", "stop"})
          .SetSelectorOptions({.group_name = "cvd_1"})
          .Build();
  ASSERT_THAT(stop_req, IsOk());
  CvdStopCommandHandler stop_handler(ctx.instance_manager);
  Result<void> stop_res = stop_handler.Handle(*stop_req);
  ASSERT_FALSE(stop_res.has_value());
  EXPECT_THAT(stop_res.error().Message(),
              HasSubstr("Selected group is not running"));

  Result<LocalInstanceGroup> after_stop =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(after_stop, IsOk());
  EXPECT_EQ(after_stop->Instances()[0].State(), cvd::INSTANCE_STATE_STOPPED);

  // Mark stale RUNNING again and verify `cvd rm` removes it cleanly without
  // needing a working `stop_cvd` binary.
  after_stop->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*after_stop), IsOk());

  Result<CommandRequest> rm_req =
      CommandRequestBuilder()
          .SetEnv(env)
          .AddArguments({"cvd", "rm"})
          .SetSelectorOptions({.group_name = "cvd_1"})
          .Build();
  ASSERT_THAT(rm_req, IsOk());
  RemoveCvdCommandHandler rm_handler(ctx.instance_manager);
  ASSERT_THAT(rm_handler.Handle(*rm_req), IsOk());

  Result<std::vector<LocalInstanceGroup>> remaining =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(remaining, IsOk());
  EXPECT_TRUE(remaining->empty());
  EXPECT_FALSE(
      DirectoryExists(after_stop->BaseDir(), /*follow_symlinks=*/false));
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

  SharedFD in_progress_lock;
  Result<LocalInstanceGroup> preparing_group =
      ctx.instance_manager.CreateInstanceGroup(
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
          &in_progress_lock);
  ASSERT_THAT(preparing_group, IsOk());
  ASSERT_TRUE(in_progress_lock->IsOpen());

  // Concurrent `cvd create --reuse=true` on another slot triggers
  // `PruneBrokenInstanceGroups()`, which must NOT prune `loading_grp` while
  // `in_progress_lock` is held.
  Result<std::string> slot2 = ws.MakeArtifactsDir("acloud_cf_51");
  ASSERT_THAT(slot2, IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot2}},
                               {"--reuse=true", "--base_instance_num=51"}),
              IsOk());

  Result<LocalInstanceGroup> still_preparing =
      ctx.instance_manager.FindGroup({.group_name = "loading_grp"});
  ASSERT_THAT(still_preparing, IsOk());
  EXPECT_EQ(still_preparing->Instances()[0].State(),
            cvd::INSTANCE_STATE_PREPARING);
  EXPECT_TRUE(DirectoryExists(still_preparing->BaseDir()));

  // Once `in_progress_lock` is dropped (e.g. `cvd load` was SIGKILLed mid-fetch
  // leaving empty artifacts), `PruneBrokenInstanceGroups` cleans it up.
  in_progress_lock = SharedFD();
  ASSERT_THAT(ctx.instance_manager.PruneBrokenInstanceGroups(), IsOk());
  Result<std::vector<LocalInstanceGroup>> after_prune =
      ctx.instance_manager.FindGroups({.group_name = "loading_grp"});
  ASSERT_THAT(after_prune, IsOk());
  EXPECT_TRUE(after_prune->empty());
}

TEST(CvdCreateReuseTest,
     DefaultReuseFalsePrunesBrokenStaleGroupsAndRecyclesInstanceId) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> temp1 = ws.MakeArtifactsDir("ephemeral_1");
  ASSERT_THAT(temp1, IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *temp1}},
                               {"--reuse=false"}),
              IsOk());

  Result<LocalInstanceGroup> group1 =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group1, IsOk());
  EXPECT_EQ(group1->Instances()[0].Id(), 1u);
  group1->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*group1), IsOk());
  ASSERT_THAT(RecursivelyRemoveDirectory(*temp1), IsOk());

  Result<std::string> temp2 = ws.MakeArtifactsDir("ephemeral_2");
  ASSERT_THAT(temp2, IsOk());
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *temp2}},
                               {"--reuse=false"}),
              IsOk());

  Result<std::vector<LocalInstanceGroup>> groups =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(groups, IsOk());
  ASSERT_EQ(groups->size(), 1u);
  EXPECT_EQ((*groups)[0].Instances()[0].Id(), 1u);
}

TEST(CvdCreateReuseTest, MultiSlotPerInstanceNameIsolationAndMismatch) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot1 = ws.MakeArtifactsDir("slot_phone_1");
  Result<std::string> slot2 = ws.MakeArtifactsDir("slot_phone_2");
  ASSERT_THAT(slot1, IsOk());
  ASSERT_THAT(slot2, IsOk());

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot1}},
                               {"--reuse=true", "--base_instance_num=1"},
                               {.instance_names = {{"phone"}}}),
              IsOk());
  Result<LocalInstanceGroup> grp1 =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(grp1, IsOk());
  grp1->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*grp1), IsOk());
  Result<Fd> mon1 =
      StartMonitorSocketForInstance(ws.Root(), grp1->Instances()[0]);
  ASSERT_THAT(mon1, IsOk());

  // Second slot with the SAME `--instance_name=phone` creates its own group.
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot2}},
                               {"--reuse=true", "--base_instance_num=2"},
                               {.instance_names = {{"phone"}}}),
              IsOk());
  Result<std::vector<LocalInstanceGroup>> groups =
      ctx.instance_manager.FindGroups({});
  ASSERT_THAT(groups, IsOk());
  EXPECT_EQ(groups->size(), 2u);

  // Explicit `--group_name=cvd_2` with mismatched `--instance_name=tablet`
  // fails.
  Result<void> name_mismatch =
      RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot2}},
                       {"--reuse=true", "--base_instance_num=2"},
                       {.group_name = "cvd_2", .instance_names = {{"tablet"}}});
  ASSERT_FALSE(name_mismatch.has_value());
  EXPECT_THAT(name_mismatch.error().Message(),
              HasSubstr("Trying to change the instances"));
}

TEST(CvdCreateReuseTest,
     UnrelatedActiveGroupWithDeletedCustomHomeDoesNotBreakOtherSlots) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot17 = ws.MakeArtifactsDir("acloud_cf_17");
  Result<std::string> slot44 = ws.MakeArtifactsDir("acloud_cf_44");
  ASSERT_THAT(slot17, IsOk());
  ASSERT_THAT(slot44, IsOk());

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot17}},
                               {"--reuse=true", "--base_instance_num=17"}),
              IsOk());
  Result<LocalInstanceGroup> grp17 =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(grp17, IsOk());
  // Hold `in_progress.lock` so `grp17` stays active even after its custom HOME
  // directory is deleted on disk (creating a dangling `<BaseDir>/home`
  // symlink).
  Result<SharedFD> lock17 = InstanceManager::AcquireGroupInProgressLock(*grp17);
  ASSERT_THAT(lock17, IsOk());
  grp17->SetAllStates(cvd::INSTANCE_STATE_RUNNING);
  ASSERT_THAT(ctx.instance_manager.UpdateInstanceGroup(*grp17), IsOk());
  ASSERT_THAT(RecursivelyRemoveDirectory(*slot17), IsOk());

  EXPECT_THAT(RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot44}},
                               {"--reuse=true", "--base_instance_num=44"}),
              IsOk());
}

TEST(CvdCreateReuseTest,
     DefaultHomeStoppedGroupPreservesUserdataOnPathMismatch) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> sys_home = SystemWideUserHome();
  ASSERT_THAT(sys_home, IsOk());
  Result<std::string> prod_a = ws.MakeArtifactsDir("default_prod_a");
  Result<std::string> prod_b = ws.MakeArtifactsDir("default_prod_b");
  ASSERT_THAT(prod_a, IsOk());
  ASSERT_THAT(prod_b, IsOk());

  std::unordered_map<std::string, std::string> env = {{"HOME", *sys_home}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--host_path=" + *prod_a,
                                "--product_path=" + *prod_a}),
              IsOk());

  Result<LocalInstanceGroup> grp =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(grp, IsOk());
  std::string userdata_sentinel = absl::StrCat(grp->HomeDir(), "/userdata.img");
  ASSERT_THAT(WriteNewFile(userdata_sentinel, "precious-userdata"), IsOk());

  Result<void> mismatch = RunCreateNoStart(
      ctx.instance_manager, env,
      {"--reuse=true", "--host_path=" + *prod_a, "--product_path=" + *prod_b});
  ASSERT_FALSE(mismatch.has_value());
  EXPECT_THAT(mismatch.error().Message(),
              HasSubstr("Trying to change the host paths"));
  EXPECT_TRUE(FileExists(userdata_sentinel));
}

TEST(CvdCreateReuseTest, DuplicateCustomHomeAcrossGroupsIsRejectedByDatabase) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("shared_slot");
  Result<std::string> slot2 = ws.MakeArtifactsDir("other_slot");
  ASSERT_THAT(slot, IsOk());
  ASSERT_THAT(slot2, IsOk());
  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};

  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=false", "--base_instance_num=1"}),
              IsOk());
  Result<void> dup_home = RunCreateNoStart(
      ctx.instance_manager, env, {"--reuse=false", "--base_instance_num=2"});
  ASSERT_FALSE(dup_home.has_value());
  EXPECT_THAT(dup_home.error().Message(),
              HasSubstr("An instance group already exists with HOME"));
  // Newly allocated lock for failed group (`2`) must be rolled back to
  // `kNotInUse`!
  Result<InstanceLockFile> lock2 = ctx.lock_manager.AcquireLock(2);
  ASSERT_THAT(lock2, IsOk());
  EXPECT_THAT(lock2->Status(), IsOkAndValue(InUseState::kNotInUse));

  // Same-user duplicate instance ID (`1`) must fail without releasing group 1's
  // existing `kInUse` lock!
  Result<void> dup_id =
      RunCreateNoStart(ctx.instance_manager, {{"HOME", *slot2}},
                       {"--reuse=false", "--base_instance_num=1"});
  ASSERT_FALSE(dup_id.has_value());
  EXPECT_TRUE(FileExists(absl::StrCat(ctx.locks_dir, "local-instance-1.lock")));
  Result<InstanceLockFile> lock1 = ctx.lock_manager.AcquireLock(1);
  ASSERT_THAT(lock1, IsOk());
  Result<InUseState> status1 = lock1->Status();
  ASSERT_THAT(status1, IsOk());
  EXPECT_EQ(*status1, InUseState::kInUse);
}

TEST(CvdCreateReuseTest, RemoveCvdRejectsGroupWhenTransitionIsInProgress) {
  ScopedTestWorkspace ws;
  ASSERT_TRUE(ws.Valid());
  HermeticCvdContext ctx(ws.Root());

  Result<std::string> slot = ws.MakeArtifactsDir("acloud_cf_60");
  ASSERT_THAT(slot, IsOk());
  std::unordered_map<std::string, std::string> env = {{"HOME", *slot}};
  ASSERT_THAT(RunCreateNoStart(ctx.instance_manager, env,
                               {"--reuse=true", "--base_instance_num=60"}),
              IsOk());

  Result<LocalInstanceGroup> group =
      ctx.instance_manager.FindGroup({.group_name = "cvd_1"});
  ASSERT_THAT(group, IsOk());
  Result<SharedFD> in_progress =
      InstanceManager::AcquireGroupInProgressLock(*group);
  ASSERT_THAT(in_progress, IsOk());

  Result<CommandRequest> rm_req =
      CommandRequestBuilder()
          .SetEnv(env)
          .AddArguments({"cvd", "rm"})
          .SetSelectorOptions({.group_name = "cvd_1"})
          .Build();
  ASSERT_THAT(rm_req, IsOk());
  RemoveCvdCommandHandler rm_handler(ctx.instance_manager);
  Result<void> rm_res = rm_handler.Handle(*rm_req);
  ASSERT_FALSE(rm_res.has_value());
  EXPECT_THAT(rm_res.error().Message(),
              HasSubstr("Group transition is currently in progress"));
}

}  // namespace
}  // namespace cuttlefish

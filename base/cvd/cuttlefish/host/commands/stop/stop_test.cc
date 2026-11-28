//
// Copyright (C) 2026 The Android Open Source Project
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

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/posix/can_access.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/temp_failure_retry.h"  // IWYU pragma: keep
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/process/subprocess_options.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

#ifdef __linux__
TEST(StopCvdFallbackTest, SparesParentPgidAndKillsLeakedInstancePgid) {
  if (!CanAccess("/usr/bin/lsof", X_OK).has_value()) {
    GTEST_SKIP() << "/usr/bin/lsof is not installed";
  }

  const std::string stop_bin = absl::StrCat(
      CurrentDirectory(), "/cuttlefish/host/commands/stop/cvd_internal_stop");
  CF_ASSERT(CanAccess(stop_bin, X_OK));

  std::string tmpl =
      absl::StrCat(testing::TempDir(), "/cf_stop_fb_pgid.XXXXXX");
  char* created = mkdtemp(tmpl.data());
  ASSERT_NE(created, nullptr) << StrError();
  const std::string home_dir(created);
  absl::Cleanup cleanup_dir = [&home_dir]() {
    (void)RecursivelyRemoveDirectory(home_dir);
  };

  const std::string runtime_dir =
      absl::StrCat(home_dir, "/cuttlefish_runtime.1");
  CF_ASSERT(
      EnsureDirectoryExists(absl::StrCat(home_dir, "/cuttlefish_assembly")));
  CF_ASSERT(EnsureDirectoryExists(runtime_dir));

  // Spawn a leaked instance process in its own session/PGID holding an open
  // file inside cuttlefish_runtime.1 so lsof +D discovers it.
  auto [leaked_ready_read, leaked_ready_write] = CF_ASSERT(Fd::Pipe());
  auto [leaked_hold_read, leaked_hold_write] = CF_ASSERT(Fd::Pipe());

  const pid_t leaked_pid = fork();
  ASSERT_GE(leaked_pid, 0);
  if (leaked_pid == 0) {
    leaked_ready_read.Close();
    leaked_hold_write.Close();
    if (setsid() < 0) {
      _exit(10);
    }
    const Result<Fd, int> held_fd = Fd::Open(
        absl::StrCat(runtime_dir, "/held_file"), O_CREAT | O_RDWR, 0644);
    if (!held_fd.has_value()) {
      _exit(11);
    }
    const char ready = '1';
    (void)leaked_ready_write.Write(&ready, 1);
    char unused = 0;
    (void)leaked_hold_read.Read(&unused, 1);
    _exit(0);
  }
  leaked_ready_write.Close();
  leaked_hold_read.Close();
  bool leaked_reaped = false;
  absl::Cleanup cleanup_leaked = [&]() {
    if (!leaked_reaped) {
      killpg(leaked_pid, SIGKILL);
      int status = 0;
      TEMP_FAILURE_RETRY(waitpid(leaked_pid, &status, 0));
    }
  };
  char ready = 0;
  ASSERT_EQ(CF_ASSERT(leaked_ready_read.Read(&ready, 1)), 1);

  // Spawn a parent harness in its own session/PGID with cwd inside
  // cuttlefish_runtime.1, launching cvd_internal_stop with InGroup(true) and
  // no config file so StopCvdMain enters FallBackStop(FallbackDirs()).
  const pid_t harness_pid = fork();
  ASSERT_GE(harness_pid, 0);
  if (harness_pid == 0) {
    leaked_hold_write.Close();
    if (setsid() < 0) {
      _exit(20);
    }
    if (chdir(runtime_dir.c_str()) != 0) {
      _exit(21);
    }
    Command cmd = Command(stop_bin)
                      .SetEnvironment({})
                      .AddEnvironmentVariable("HOME", home_dir)
                      .AddEnvironmentVariable("PATH", "/usr/bin:/bin")
                      .AddParameter("--wait_for_launcher=1");
    const int rc =
        RunWithManagedStdio(std::move(cmd), nullptr, nullptr, nullptr,
                            SubprocessOptions().InGroup(true));
    _exit(rc == 0 ? 0 : 22);
  }

  int harness_status = 0;
  ASSERT_EQ(TEMP_FAILURE_RETRY(waitpid(harness_pid, &harness_status, 0)),
            harness_pid)
      << StrError();
  ASSERT_TRUE(WIFEXITED(harness_status))
      << "Parent harness killed by signal " << WTERMSIG(harness_status);
  EXPECT_EQ(WEXITSTATUS(harness_status), 0);

  // Close the hold pipe so an unkilled leaked child exits promptly instead of
  // hanging; Linux delivers pending SIGKILL before returning from read(2).
  leaked_hold_write.Close();
  int leaked_status = 0;
  ASSERT_EQ(TEMP_FAILURE_RETRY(waitpid(leaked_pid, &leaked_status, 0)),
            leaked_pid)
      << StrError();
  leaked_reaped = true;
  ASSERT_TRUE(WIFSIGNALED(leaked_status))
      << "Expected leaked process to be killed by signal, wstatus="
      << leaked_status;
  EXPECT_EQ(WTERMSIG(leaked_status), SIGKILL);
}
#endif

}  // namespace
}  // namespace cuttlefish

//
// Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/process/proc_file_utils.h"

#include <sys/types.h>
#ifdef __linux__
#include <errno.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif
#include <unistd.h>

#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::IsEmpty;
using ::testing::Not;

TEST(ProcFileUid, SelfUidTest) {
  auto my_pid = getpid();
  auto login_uid_of_my_pid = OwnerUid(my_pid);

  ASSERT_THAT(login_uid_of_my_pid, IsOk());
  ASSERT_EQ(getuid(), *login_uid_of_my_pid);
}

TEST(ProcFilePid, CurrentPidCollected) {
  auto pids_result = CollectPids(getuid());
  auto this_pid = getpid();

  // verify if the pids returned are really owned by getuid()
  ASSERT_TRUE(pids_result.has_value());
  ASSERT_TRUE(Contains(*pids_result, this_pid));
}

TEST(ProcFileCmdArgs, CurrentProcessHasArgs) {
  Result<std::vector<std::string>> args = GetCmdArgs(getpid());
  ASSERT_THAT(args, IsOk());
  EXPECT_THAT(*args, Not(IsEmpty()));
}

#ifdef __linux__
TEST(ProcFileCmdArgs, NonDumpableProcessArgsReadable) {
  Result<std::vector<std::string>> self_args = GetCmdArgs(getpid());
  ASSERT_THAT(self_args, IsOk());
  ASSERT_THAT(*self_args, Not(IsEmpty()));

  int ready_pipe[2] = {-1, -1};
  int exit_pipe[2] = {-1, -1};
  ASSERT_EQ(pipe(ready_pipe), 0);
  absl::Cleanup close_ready = [&ready_pipe]() {
    if (ready_pipe[0] != -1) {
      close(ready_pipe[0]);
    }
    if (ready_pipe[1] != -1) {
      close(ready_pipe[1]);
    }
  };
  ASSERT_EQ(pipe(exit_pipe), 0);
  absl::Cleanup close_exit = [&exit_pipe]() {
    if (exit_pipe[0] != -1) {
      close(exit_pipe[0]);
    }
    if (exit_pipe[1] != -1) {
      close(exit_pipe[1]);
    }
  };

  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    close(ready_pipe[0]);
    close(exit_pipe[1]);
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
      _exit(1);
    }
    char ready = '1';
    if (write(ready_pipe[1], &ready, 1) != 1) {
      _exit(2);
    }
    close(ready_pipe[1]);
    char done = 0;
    (void)read(exit_pipe[0], &done, 1);
    close(exit_pipe[0]);
    _exit(0);
  }

  close(ready_pipe[1]);
  ready_pipe[1] = -1;
  close(exit_pipe[0]);
  exit_pipe[0] = -1;
  absl::Cleanup reap_child = [child, &exit_pipe]() {
    if (exit_pipe[1] != -1) {
      close(exit_pipe[1]);
      exit_pipe[1] = -1;
    }
    int status = 0;
    while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
    }
  };

  char ready = 0;
  ASSERT_EQ(read(ready_pipe[0], &ready, 1), 1);

  if (geteuid() != 0) {
    EXPECT_THAT(GetExecutablePath(child), IsError());
  }
  Result<std::vector<std::string>> child_args = GetCmdArgs(child);
  ASSERT_THAT(child_args, IsOk());
  EXPECT_EQ(*child_args, *self_args);
}
#endif

}  // namespace
}  // namespace cuttlefish

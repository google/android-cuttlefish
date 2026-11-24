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
#include <unistd.h>

#include <string>
#include <tuple>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

#ifdef __linux__
#include <errno.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#endif

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
  std::vector<std::string> args = CF_ASSERT(GetCmdArgs(getpid()));
  EXPECT_THAT(args, Not(IsEmpty()));
}

#ifdef __linux__
TEST(ProcFileCmdArgs, NonDumpableProcessArgsReadable) {
  std::vector<std::string> self_args = CF_ASSERT(GetCmdArgs(getpid()));
  ASSERT_THAT(self_args, Not(IsEmpty()));

  Fd ready_read;
  Fd ready_write;
  std::tie(ready_read, ready_write) = CF_ASSERT(Fd::Pipe());
  Fd exit_read;
  Fd exit_write;
  std::tie(exit_read, exit_write) = CF_ASSERT(Fd::Pipe());

  pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    ready_read.Close();
    exit_write.Close();
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
      _exit(1);
    }
    char ready = '1';
    if (ready_write.Write(&ready, 1).value_or(0) != 1) {
      _exit(2);
    }
    ready_write.Close();
    char done = 0;
    (void)exit_read.Read(&done, 1);
    exit_read.Close();
    _exit(0);
  }

  ready_write.Close();
  exit_read.Close();
  absl::Cleanup reap_child = [child, &exit_write]() {
    exit_write.Close();
    int status = 0;
    while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
    }
  };

  char ready = 0;
  ASSERT_EQ(CF_ASSERT(ready_read.Read(&ready, 1)), 1);

  if (geteuid() != 0) {
    EXPECT_THAT(GetExecutablePath(child), IsError());
  }
  std::vector<std::string> child_args = CF_ASSERT(GetCmdArgs(child));
  EXPECT_EQ(child_args, self_args);
}
#endif

}  // namespace
}  // namespace cuttlefish

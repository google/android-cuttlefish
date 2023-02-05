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

#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/proc_file_utils.h"

namespace cuttlefish {

TEST(ProcFileUid, SelfUidTest) {
  auto my_pid = getpid();
  auto login_uid_of_my_pid = OwnerUid(my_pid);

  ASSERT_TRUE(login_uid_of_my_pid.ok()) << login_uid_of_my_pid.error().Trace();
  ASSERT_EQ(getuid(), *login_uid_of_my_pid);
}

TEST(ProcFilePid, CollectAllProcesses) {
  auto pids_result = CollectPids(getuid());

  // verify if the pids returned are really owned by getuid()
  ASSERT_TRUE(pids_result.ok());
  for (const auto pid : *pids_result) {
    auto proc_uid = OwnerUid(pid);
    ASSERT_TRUE(proc_uid.ok()) << proc_uid.error().Trace();
    ASSERT_EQ(*proc_uid, getuid());
  }
}

TEST(ProcFilePid, CurrentPidCollected) {
  auto pids_result = CollectPids(getuid());
  auto this_pid = getpid();

  // verify if the pids returned are really owned by getuid()
  ASSERT_TRUE(pids_result.ok());
  ASSERT_TRUE(Contains(*pids_result, this_pid));
}

}  // namespace cuttlefish

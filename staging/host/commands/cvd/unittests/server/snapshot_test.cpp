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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/libs/utils/contains.h"
#include "host/commands/cvd/types.h"
#include "host/commands/cvd/unittests/server/cmd_runner.h"
#include "host/commands/cvd/unittests/server/snapshot_test_helper.cpp"

namespace cuttlefish {
namespace cvdsnapshot {

TEST_F(CvdSnapshotTest, CvdSuspendResume) {
  auto cmd_suspend = CmdRunner::Run("cvd suspend", envs);
  ASSERT_TRUE(cmd_suspend.Success()) << cmd_suspend.Stderr();

  auto cmd_resume = CmdRunner::Run("cvd resume", envs);
  ASSERT_TRUE(cmd_resume.Success()) << cmd_resume.Stderr();

  auto cmd_stop = CmdRunner::Run("cvd stop", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();
}

TEST_F(CvdSnapshotTest, CvdSuspendSnapshotResume) {
  auto cmd_suspend = CmdRunner::Run("cvd suspend", envs);
  ASSERT_TRUE(cmd_suspend.Success()) << cmd_suspend.Stderr();

  auto cmd_snapshot = CmdRunner::Run(
      "cvd snapshot_take --snapshot_path=/tmp/snapshots/snapshot", envs);
  ASSERT_TRUE(cmd_snapshot.Success()) << cmd_snapshot.Stderr();

  auto cmd_resume = CmdRunner::Run("cvd resume", envs);
  ASSERT_TRUE(cmd_resume.Success()) << cmd_resume.Stderr();

  auto cmd_stop = CmdRunner::Run("cvd stop", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();

  auto cmd_rm = CmdRunner::Run("rm -rf /tmp/snapshots/snapshot", envs);
  ASSERT_TRUE(cmd_rm.Success()) << cmd_rm.Stderr();
}

TEST_F(CvdSnapshotTest, CvdSuspendSnapshotResumeRestore) {
  auto cmd_suspend = CmdRunner::Run("cvd suspend", envs);
  ASSERT_TRUE(cmd_suspend.Success()) << cmd_suspend.Stderr();

  auto cmd_snapshot = CmdRunner::Run(
      "cvd snapshot_take --snapshot_path=/tmp/snapshots/snapshot", envs);
  ASSERT_TRUE(cmd_snapshot.Success()) << cmd_snapshot.Stderr();

  auto cmd_stop = CmdRunner::Run("cvd stop", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();

  // TODO(khei): un-comment the remaining lines after aosp/2726020 is merged
  // // clean up for the next test
  // CmdRunner::Run("cvd reset -y", envs);

  // cvd_common::Args start_args{"cvd", "start",
  //                             "--report_anonymous_usage_stats=yes",
  //                             "--daemon",
  //                             "--snapshot_path=/tmp/snapshots/snapshot"};

  // auto cmd_start_2 = CmdRunner::Run(start_args, envs);
  // ASSERT_TRUE(cmd_start_2.Success()) << cmd_start_2.Stderr();

  // auto cmd_stop_2 = CmdRunner::Run("cvd stop", envs);
  // ASSERT_TRUE(cmd_stop_2.Success()) << cmd_stop_2.Stderr();

  auto cmd_rm = CmdRunner::Run("rm -rf /tmp/snapshots/snapshot", envs);
  ASSERT_TRUE(cmd_rm.Success()) << cmd_rm.Stderr();
}

}  // namespace cvdsnapshot
}  // namespace cuttlefish

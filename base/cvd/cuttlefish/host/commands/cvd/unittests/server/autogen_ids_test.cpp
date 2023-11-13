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
#include "host/commands/cvd/unittests/server/utils.h"

namespace cuttlefish {

TEST(CvdAutoGenId, CvdTwoFollowedByFive) {
  cvd_common::Envs envs;
  envs["HOME"] = StringFromEnv("HOME", "");
  CmdRunner::Run("cvd reset -y", envs);

  cvd_common::Args start_two_instances_args{
      "cvd",
      "start",
      "--report_anonymous_usage_stats=yes",
      "--daemon",
      "--norestart_subprocesses",
      "--num_instances=2"};
  cvd_common::Args start_three_instances_args{
      "cvd",
      "start",
      "--report_anonymous_usage_stats=yes",
      "--daemon",
      "--norestart_subprocesses",
      "--num_instances=3"};

  auto cmd_start_two = CmdRunner::Run(start_two_instances_args, envs);
  ASSERT_TRUE(cmd_start_two.Success()) << cmd_start_two.Stderr();
  auto cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_TRUE(cmd_fleet.Success()) << cmd_fleet.Stderr();
  ASSERT_EQ(NumberOfOccurrences(cmd_fleet.Stdout(), "instance_name"), 2)
      << cmd_fleet.Stdout();

  auto cmd_start_three = CmdRunner::Run(start_three_instances_args, envs);
  ASSERT_TRUE(cmd_start_three.Success()) << cmd_start_three.Stderr();
  cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_TRUE(cmd_fleet.Success()) << cmd_fleet.Stderr();
  ASSERT_EQ(NumberOfOccurrences(cmd_fleet.Stdout(), "instance_name"), 5)
      << cmd_fleet.Stdout();

  auto cmd_stop = CmdRunner::Run("cvd reset -y", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();

  cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_FALSE(Contains(cmd_fleet.Stdout(), "instance_name"));

  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);
}

}  // namespace cuttlefish

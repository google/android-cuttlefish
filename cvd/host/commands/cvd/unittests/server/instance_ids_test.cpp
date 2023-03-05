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

TEST(CvdInstanceIds, CvdTakenInstanceIds) {
  cvd_common::Envs envs;
  envs["HOME"] = StringFromEnv("HOME", "");
  CmdRunner::Run("cvd reset -y", envs);

  cvd_common::Args start_1_2_args{"cvd",
                                  "--disable_default_group",
                                  "start",
                                  "--report_anonymous_usage_stats=yes",
                                  "--daemon",
                                  "--norestart_subprocesses",
                                  "--instance_nums=1,2"};
  cvd_common::Args start_3_args{"cvd",
                                "--disable_default_group",
                                "start",
                                "--report_anonymous_usage_stats=yes",
                                "--daemon",
                                "--norestart_subprocesses",
                                "--instance_nums=3"};
  cvd_common::Args start_4_5_6_args{"cvd",
                                    "--disable_default_group",
                                    "start",
                                    "--report_anonymous_usage_stats=yes",
                                    "--daemon",
                                    "--norestart_subprocesses",
                                    "--instance_nums=4,5,6"};
  cvd_common::Args start_5_7_args{"cvd",
                                  "--disable_default_group",
                                  "start",
                                  "--report_anonymous_usage_stats=yes",
                                  "--daemon",
                                  "--norestart_subprocesses",
                                  "--instance_nums=4,5,6"};

  auto cmd_start_1_2 = CmdRunner::Run(start_1_2_args, envs);
  auto cmd_start_3 = CmdRunner::Run(start_3_args, envs);
  auto cmd_start_4_5_6 = CmdRunner::Run(start_4_5_6_args, envs);
  ASSERT_TRUE(cmd_start_1_2.Success()) << cmd_start_1_2.Stderr();
  ASSERT_TRUE(cmd_start_3.Success()) << cmd_start_3.Stderr();
  ASSERT_TRUE(cmd_start_4_5_6.Success()) << cmd_start_4_5_6.Stderr();

  auto cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_TRUE(cmd_fleet.Success()) << cmd_fleet.Stderr();
  ASSERT_EQ(NumberOfOccurrences(cmd_fleet.Stdout(), "instance_name"), 6)
      << cmd_fleet.Stdout();

  auto cmd_3_to_fail = CmdRunner::Run(start_3_args, envs);
  auto cmd_5_7_to_fail = CmdRunner::Run(start_5_7_args, envs);
  ASSERT_TRUE(cmd_start_3.Success()) << cmd_start_3.Stderr();
  ASSERT_TRUE(cmd_start_4_5_6.Success()) << cmd_start_4_5_6.Stderr();

  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);
}

}  // namespace cuttlefish

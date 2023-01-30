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

namespace cuttlefish {

TEST(CvdBasic, CvdDefaultStart) {
  cvd_common::Envs envs;
  const auto home_dir = StringFromEnv("HOME", "");
  envs["HOME"] = home_dir;
  CmdRunner::Run("cvd kill-server", envs);

  cvd_common::Args start_args{"cvd", "start",
                              "--report_anonymous_usage_stats=yes", "--daemon"};

  auto cmd_start = CmdRunner::Run(start_args, envs);
  ASSERT_TRUE(cmd_start.Success()) << cmd_start.Stderr();

  auto cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_TRUE(cmd_fleet.Success()) << cmd_fleet.Stderr();
  ASSERT_TRUE(Contains(cmd_fleet.Stdout(), home_dir));

  auto cmd_stop = CmdRunner::Run("cvd stop", envs);
  ASSERT_TRUE(cmd_stop.Success()) << cmd_stop.Stderr();

  cmd_fleet = CmdRunner::Run("cvd fleet", envs);
  ASSERT_FALSE(Contains(cmd_fleet.Stdout(), home_dir));

  // clean up for the next test
  CmdRunner::Run("cvd kill-server", envs);
}

}  // namespace cuttlefish

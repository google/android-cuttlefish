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

#include <gtest/gtest.h>

#include "host/commands/cvd/unittests/server/cmd_runner.h"

#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace cvdsnapshot {

class CvdSnapshotTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CmdRunner::Run("cvd reset -y", envs);

    cvd_common::Args start_args{
        "cvd", "start", "--report_anonymous_usage_stats=yes", "--daemon"};

    auto cmd_start = CmdRunner::Run(start_args, envs);
    ASSERT_TRUE(cmd_start.Success()) << cmd_start.Stderr();
  };
  void TearDown() override {
    // clean up for the next test
    CmdRunner::Run("cvd reset -y", envs);
  }
  cvd_common::Envs envs;
};

}  // namespace cvdsnapshot
}  // namespace cuttlefish

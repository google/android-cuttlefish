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
namespace {

bool ContainsAll(const std::string& stream,
                 const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (!Contains(stream, token)) {
      return false;
    }
  }
  return true;
}

/*
 * Sees if this might be cvd --help output.
 *
 * Not very accurate.
 */
bool MaybeCvdHelp(const CmdResult& result) {
  const auto& stdout = result.Stdout();
  return ContainsAll(stdout, {"help", "start", "stop", "fleet"});
}

bool MaybeCvdStop(const CmdResult& result) {
  const auto& stderr = result.Stderr();
  const auto& stdout = result.Stdout();
  return Contains(stderr, "cvd_internal_stop") ||
         Contains(stdout, "cvd_internal_stop") ||
         Contains(stderr, "stop_cvd") || Contains(stdout, "stop_cvd");
}

bool MaybeCvdStart(const CmdResult& result) {
  const auto& stdout = result.Stdout();
  return ContainsAll(stdout, {"vhost", "modem", "daemon", "adb"});
}

}  // namespace

TEST(CvdDriver, CvdHelp) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset -y", envs);

  auto cmd_help = CmdRunner::Run("cvd help", envs);
  auto cmd_dash_help = CmdRunner::Run("cvd --help", envs);

  ASSERT_TRUE(cmd_help.Success()) << cmd_help.Stderr();
  ASSERT_TRUE(MaybeCvdHelp(cmd_help));
  ASSERT_TRUE(cmd_dash_help.Success()) << cmd_dash_help.Stderr();
  ASSERT_TRUE(MaybeCvdHelp(cmd_dash_help));

  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);
}

TEST(CvdDriver, CvdOnly) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset -y", envs);

  auto cmd_help = CmdRunner::Run("cvd help", envs);
  auto cmd_only = CmdRunner::Run("cvd", envs);

  ASSERT_TRUE(cmd_help.Success()) << cmd_help.Stderr();
  ASSERT_TRUE(cmd_only.Success()) << cmd_only.Stderr();
  ASSERT_EQ(cmd_help.Stdout(), cmd_only.Stdout());

  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);
}

// this test is expected to fail. included proactively.
TEST(CvdDriver, CvdHelpWrong) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset -y", envs);

  auto cmd_help_ref = CmdRunner::Run("cvd help", envs);
  auto cmd_help_wrong = CmdRunner::Run("cvd help not_exist", envs);

  EXPECT_TRUE(cmd_help_ref.Success()) << cmd_help_ref.Stderr();
  EXPECT_TRUE(cmd_help_wrong.Success()) << cmd_help_wrong.Stderr();
  EXPECT_EQ(cmd_help_ref.Stdout(), cmd_help_wrong.Stdout());

  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);
}

TEST(CvdSubtool, CvdStopHelp) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset -y", envs);

  auto cmd_stop_help = CmdRunner::Run("cvd help stop", envs);

  ASSERT_TRUE(cmd_stop_help.Success()) << cmd_stop_help.Stderr();
  ASSERT_TRUE(MaybeCvdStop(cmd_stop_help))
      << "stderr: " << cmd_stop_help.Stderr()
      << "stdout: " << cmd_stop_help.Stdout();

  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);
}

TEST(CvdSubtool, CvdStartHelp) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset -y", envs);

  auto cmd_start_help = CmdRunner::Run("cvd help start", envs);

  ASSERT_TRUE(cmd_start_help.Success()) << cmd_start_help.Stderr();
  ASSERT_TRUE(MaybeCvdStart(cmd_start_help))
      << "stderr: " << cmd_start_help.Stderr()
      << "stdout: " << cmd_start_help.Stdout();

  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);
}

}  // namespace cuttlefish

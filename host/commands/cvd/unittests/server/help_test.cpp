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

#include "common/libs/utils/contains.h"
#include "host/commands/cvd/types.h"
#include "host/commands/cvd/unittests/server/cmd_runner.h"

namespace cuttlefish {
namespace selector {
namespace {

/*
 * Sees if this might be cvd --help output.
 *
 * Not very accurate.
 */
bool MaybeCvdHelp(const CmdResult& result) {
  const auto& stdout = result.Stdout();
  return Contains(stdout, "help") && Contains(stdout, "start") &&
         Contains(stdout, "stop") && Contains(stdout, "fleet");
}

}  // namespace

TEST(CvdDriver, CvdHelp) {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd kill-server", envs);

  auto cmd_help = CmdRunner::Run("cvd help", envs);
  auto cmd_dash_help = CmdRunner::Run("cvd --help", envs);

  ASSERT_TRUE(cmd_help.Success()) << cmd_help.Stderr();
  ASSERT_TRUE(MaybeCvdHelp(cmd_help));
  ASSERT_TRUE(cmd_dash_help.Success()) << cmd_dash_help.Stderr();
  ASSERT_TRUE(MaybeCvdHelp(cmd_dash_help));
}

}  // namespace selector
}  // namespace cuttlefish

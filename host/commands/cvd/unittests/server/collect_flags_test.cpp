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

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "host/commands/cvd/server_command/flags_collector.h"
#include "host/commands/cvd/types.h"
#include "host/commands/cvd/unittests/server/cmd_runner.h"

namespace cuttlefish {

TEST(CvdHelpFlagCollect, LauncCvd) {
  cvd_common::Envs envs;
  const auto home_dir = StringFromEnv("HOME", "");
  envs["HOME"] = home_dir;
  const auto android_host_out = StringFromEnv(
      "ANDROID_HOST_OUT",
      android::base::Dirname(android::base::GetExecutableDirectory()));
  envs["ANDROID_HOST_OUT"] = android_host_out;
  const auto launch_cvd_path = android_host_out + "/bin/launch_cvd";
  if (!FileExists(launch_cvd_path)) {
    GTEST_SKIP() << "Can't find " << launch_cvd_path << " for testing.";
  }
  CmdRunner::Run("cvd kill-server", envs);
  cvd_common::Args helpxml_args{launch_cvd_path, "--helpxml"};

  auto cmd_help_xml = CmdRunner::Run(helpxml_args, envs);
  auto flags_opt = CollectFlagsFromHelpxml(cmd_help_xml.Stdout());

  ASSERT_FALSE(cmd_help_xml.Stdout().empty()) << "output shouldn't be empty.";
  ASSERT_TRUE(flags_opt);
  auto& flags = *flags_opt;
  auto daemon_flag_itr = std::find_if(
      flags.cbegin(), flags.cend(),
      [](const FlagInfoPtr& ptr) { return (ptr && ptr->Name() == "daemon"); });
  auto bad_flag_itr = std::find_if(
      flags.cbegin(), flags.cend(),
      [](const FlagInfoPtr& ptr) { return (ptr && ptr->Name() == "@bad@"); });
  ASSERT_NE(daemon_flag_itr, flags.cend());
  ASSERT_EQ(bad_flag_itr, flags.cend());
}

}  // namespace cuttlefish

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
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/result_matchers.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"

namespace cuttlefish {

TEST(HostToolTarget, KnownFlags) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }

  auto host_tool_target = HostToolTarget::Create(android_host_out);
  EXPECT_THAT(host_tool_target, IsOk());

  auto daemon_flag =
      host_tool_target->GetFlagInfo(HostToolTarget::FlagInfoRequest{
          .operation_ = "start",
          .flag_name_ = "daemon",
      });

  auto bad_flag = host_tool_target->GetFlagInfo(HostToolTarget::FlagInfoRequest{
      .operation_ = "start",
      .flag_name_ = "@never_exist@",
  });

  EXPECT_THAT(daemon_flag, IsOk());
  ASSERT_EQ(daemon_flag->Name(), "daemon");
  ASSERT_TRUE(daemon_flag->Type() == "string" || daemon_flag->Type() == "bool");
  EXPECT_THAT(bad_flag, IsError());
}

TEST(HostToolManager, KnownFlags) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }
  auto host_tool_manager = NewHostToolTargetManager();

  auto daemon_flag =
      host_tool_manager->ReadFlag({.artifacts_path = android_host_out,
                                   .op = "start",
                                   .flag_name = "daemon"});
  auto bad_flag =
      host_tool_manager->ReadFlag({.artifacts_path = android_host_out,
                                   .op = "start",
                                   .flag_name = "@never_exist@"});

  EXPECT_THAT(daemon_flag, IsOk());
  ASSERT_EQ(daemon_flag->Name(), "daemon");
  ASSERT_TRUE(daemon_flag->Type() == "string" || daemon_flag->Type() == "bool");
  EXPECT_THAT(bad_flag, IsError());
}

TEST(HostToolManager, KnownBins) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }
  auto host_tool_manager = NewHostToolTargetManager();

  auto start_bin = host_tool_manager->ExecBaseName(
      {.artifacts_path = android_host_out, .op = "start"});
  auto stop_bin = host_tool_manager->ExecBaseName(
      {.artifacts_path = android_host_out, .op = "stop"});
  auto bad_bin = host_tool_manager->ExecBaseName(
      {.artifacts_path = android_host_out, .op = "bad"});

  EXPECT_THAT(start_bin, IsOk());
  EXPECT_THAT(stop_bin, IsOk());
  EXPECT_THAT(bad_bin, IsError());
  ASSERT_TRUE(*start_bin == "cvd_internal_start" || *start_bin == "launch_cvd")
      << "start_bin was " << *start_bin;
  ASSERT_TRUE(*stop_bin == "cvd_internal_stop" || *stop_bin == "stop_cvd")
      << "stop_bin was " << *stop_bin;
}

}  // namespace cuttlefish

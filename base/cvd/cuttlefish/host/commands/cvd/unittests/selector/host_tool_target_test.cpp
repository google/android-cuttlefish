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
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"

namespace cuttlefish {

TEST(HostToolTarget, KnownFlags) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }
  std::unordered_map<std::string, std::vector<std::string>> ops_to_op_impl_map{
      {"start", std::vector<std::string>{"cvd_internal_start", "launch_cvd"}}};

  auto host_tool_target =
      HostToolTarget::Create(android_host_out, ops_to_op_impl_map);
  ASSERT_TRUE(host_tool_target.ok()) << host_tool_target.error().Trace();

  auto daemon_flag =
      host_tool_target->GetFlagInfo(HostToolTarget::FlagInfoRequest{
          .operation_ = "start",
          .flag_name_ = "daemon",
      });

  auto bad_flag = host_tool_target->GetFlagInfo(HostToolTarget::FlagInfoRequest{
      .operation_ = "start",
      .flag_name_ = "@never_exist@",
  });

  ASSERT_TRUE(daemon_flag.ok()) << daemon_flag.error().Trace();
  ASSERT_EQ(daemon_flag->Name(), "daemon");
  ASSERT_TRUE(daemon_flag->Type() == "string" || daemon_flag->Type() == "bool");
  ASSERT_FALSE(bad_flag.ok());
}

fruit::Component<HostToolTargetManager> CreateManagerComponent() {
  return fruit::createComponent()
      .install(HostToolTargetManagerComponent)
      .install(OperationToBinsMapComponent);
}

TEST(HostToolManager, KnownFlags) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }
  fruit::Injector<HostToolTargetManager> injector(CreateManagerComponent);
  HostToolTargetManager& host_tool_manager =
      injector.get<HostToolTargetManager&>();

  auto daemon_flag =
      host_tool_manager.ReadFlag({.artifacts_path = android_host_out,
                                  .op = "start",
                                  .flag_name = "daemon"});
  auto bad_flag =
      host_tool_manager.ReadFlag({.artifacts_path = android_host_out,
                                  .op = "start",
                                  .flag_name = "@never_exist@"});

  ASSERT_TRUE(daemon_flag.ok()) << daemon_flag.error().Trace();
  ASSERT_EQ(daemon_flag->Name(), "daemon");
  ASSERT_TRUE(daemon_flag->Type() == "string" || daemon_flag->Type() == "bool");
  ASSERT_FALSE(bad_flag.ok());
}

TEST(HostToolManager, KnownBins) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }
  fruit::Injector<HostToolTargetManager> injector(CreateManagerComponent);
  HostToolTargetManager& host_tool_manager =
      injector.get<HostToolTargetManager&>();

  auto start_bin = host_tool_manager.ExecBaseName(
      {.artifacts_path = android_host_out, .op = "start"});
  auto stop_bin = host_tool_manager.ExecBaseName(
      {.artifacts_path = android_host_out, .op = "stop"});
  auto bad_bin = host_tool_manager.ExecBaseName(
      {.artifacts_path = android_host_out, .op = "bad"});

  ASSERT_TRUE(start_bin.ok()) << start_bin.error().Trace();
  ASSERT_TRUE(stop_bin.ok()) << stop_bin.error().Trace();
  ASSERT_FALSE(bad_bin.ok()) << "bad_bin should be CF_ERR but is " << *bad_bin;
  ASSERT_TRUE(*start_bin == "cvd_internal_start" || *start_bin == "launch_cvd")
      << "start_bin was " << *start_bin;
  ASSERT_TRUE(*stop_bin == "cvd_internal_stop" || *stop_bin == "stop_cvd")
      << "stop_bin was " << *stop_bin;
}

}  // namespace cuttlefish

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

#include <gtest/gtest.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"

namespace cuttlefish {

static Result<std::string> StartBin(const std::string& android_host_out) {
  if (FileExists(android_host_out + "/bin/cvd_internal_start")) {
    return "cvd_internal_start";
  }
  if (FileExists(android_host_out + "/bin/launch_cvd")) {
    return "launch_cvd";
  }
  return CF_ERR(android_host_out << " does not have launcher");
}

TEST(HostToolTarget, KnownFlags) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }
  auto start_bin_result = StartBin(android_host_out);
  if (!start_bin_result.ok()) {
    GTEST_SKIP() << start_bin_result.error().Message();
  }

  auto host_tool_target =
      HostToolTarget::Create(android_host_out, *start_bin_result);
  ASSERT_TRUE(host_tool_target.ok()) << host_tool_target.error().Trace();

  auto daemon_flag = host_tool_target->GetFlagInfo("daemon");
  auto bad_flag = host_tool_target->GetFlagInfo("@never_exist@");

  ASSERT_TRUE(daemon_flag.ok()) << daemon_flag.error().Trace();
  ASSERT_EQ(daemon_flag->Name(), "daemon");
  ASSERT_TRUE(daemon_flag->Type() == "string" || daemon_flag->Type() == "bool");
  ASSERT_FALSE(bad_flag.ok());
}

fruit::Component<HostToolTargetManager> CreateManagerComponent() {
  return fruit::createComponent();
}

TEST(HostToolManager, KnownFlags) {
  std::string android_host_out = StringFromEnv("ANDROID_HOST_OUT", "");
  if (android_host_out.empty()) {
    GTEST_SKIP() << "Set ANDROID_HOST_OUT";
  }
  auto start_bin_result = StartBin(android_host_out);
  if (!start_bin_result.ok()) {
    GTEST_SKIP() << start_bin_result.error().Message();
  }
  fruit::Injector<HostToolTargetManager> injector(CreateManagerComponent);
  HostToolTargetManager& host_tool_manager =
      injector.get<HostToolTargetManager&>();

  auto daemon_flag =
      host_tool_manager.ReadFlag({.artifacts_path = android_host_out,
                                  .start_bin = *start_bin_result,
                                  .flag_name = "daemon"});
  auto bad_flag =
      host_tool_manager.ReadFlag({.artifacts_path = android_host_out,
                                  .start_bin = *start_bin_result,
                                  .flag_name = "@never_exist@"});

  ASSERT_TRUE(daemon_flag.ok()) << daemon_flag.error().Trace();
  ASSERT_EQ(daemon_flag->Name(), "daemon");
  ASSERT_TRUE(daemon_flag->Type() == "string" || daemon_flag->Type() == "bool");
  ASSERT_FALSE(bad_flag.ok());
}

}  // namespace cuttlefish

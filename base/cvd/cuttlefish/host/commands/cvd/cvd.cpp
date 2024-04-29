/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/commands/cvd/cvd.h"

#include <android-base/file.h>
#include <android-base/logging.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/request_context.h"
#include "host/commands/cvd/server_client.h"

namespace cuttlefish {

namespace {
[[noreturn]] void CallPythonAcloud(std::vector<std::string>& args) {
  auto android_top = StringFromEnv("ANDROID_BUILD_TOP", "");
  CHECK(android_top != "") << "Could not find android environment. Please run "
                           << "\"source build/envsetup.sh\".";
  // TODO(b/206893146): Detect what the platform actually is.
  auto py_acloud_path =
      android_top + "/prebuilts/asuite/acloud/linux-x86/acloud";
  std::unique_ptr<char*[]> new_argv(new char*[args.size() + 1]);
  for (size_t i = 0; i < args.size(); i++) {
    new_argv[i] = args[i].data();
  }
  new_argv[args.size()] = nullptr;
  execv(py_acloud_path.data(), new_argv.get());
  PLOG(FATAL) << "execv(" << py_acloud_path << ", ...) failed";
  abort();
}

}  // namespace

Cvd::Cvd(const android::base::LogSeverity verbosity,
         InstanceLockFileManager& instance_lockfile_manager,
         InstanceManager& instance_manager,
         HostToolTargetManager& host_tool_target_manager)
    : verbosity_(verbosity),
      instance_lockfile_manager_(instance_lockfile_manager),
      instance_manager_(instance_manager),
      host_tool_target_manager_(host_tool_target_manager) {}

Result<cvd::Response> Cvd::HandleCommand(
    const std::vector<std::string>& cvd_process_args,
    const std::unordered_map<std::string, std::string>& env,
    const std::vector<std::string>& selector_args) {
  cvd::Request request = MakeRequest({.cmd_args = cvd_process_args,
                                      .env = env,
                                      .selector_args = selector_args},
                                     cvd::WAIT_BEHAVIOR_COMPLETE);

  RequestContext context(instance_lockfile_manager_, instance_manager_,
                         host_tool_target_manager_);
  RequestWithStdio request_with_stdio(
      request, {SharedFD::Dup(0), SharedFD::Dup(1), SharedFD::Dup(2)});
  auto handler = CF_EXPECT(context.Handler(request_with_stdio));
  return handler->Handle(request_with_stdio);
}

Result<void> Cvd::HandleCvdCommand(
    const std::vector<std::string>& all_args,
    const std::unordered_map<std::string, std::string>& env) {
  const cvd_common::Args new_cmd_args{"cvd", "process"};
  CF_EXPECT(!all_args.empty());
  const cvd_common::Args new_selector_args{all_args.begin(), all_args.end()};
  // TODO(schuffelen): Deduplicate cvd process split.
  CF_EXPECT(HandleCommand(new_cmd_args, env, new_selector_args));
  return {};
}

Result<void> Cvd::HandleAcloud(
    const std::vector<std::string>& args,
    const std::unordered_map<std::string, std::string>& env) {
  std::vector<std::string> args_copy{args};
  args_copy[0] = "try-acloud";

  auto attempt = HandleCommand(args_copy, env, {});
  if (!attempt.ok()) {
    CallPythonAcloud(args_copy);
    // no return
  }

  args_copy[0] = "acloud";
  CF_EXPECT(HandleCommand(args_copy, env, {}));

  return {};
}

}  // namespace cuttlefish

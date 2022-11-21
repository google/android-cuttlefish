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

#include "host/commands/cvd/server_command_start_impl.h"

#include <sys/types.h>

#include <cstdint>
#include <cstdlib>

#include <android-base/parseint.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

Result<bool> CvdStartCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(command_to_binary_map_, invocation.command);
}

Result<cvd::Response> CvdStartCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  if (interrupted_) {
    return CF_ERR("Interrupted");
  }
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(request.Credentials() != std::nullopt);
  const uid_t uid = request.Credentials()->uid;

  cvd::Response response;
  response.mutable_command_response();

  auto invocation_info_opt = ExtractInfo(command_to_binary_map_, request);
  if (!invocation_info_opt) {
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        "ANDROID_HOST_OUT in client environment is invalid.");
    return response;
  }

  auto invocation_info = std::move(*invocation_info_opt);
  const std::string bin_path =
      CF_EXPECT(UpdateInstanceDatabase(invocation_info))
          ? CF_EXPECT(MakeBinPathFromDatabase(invocation_info))
          : invocation_info.host_artifacts_path + "/bin/" + invocation_info.bin;

  Command command = CF_EXPECT(ConstructCommand(
      bin_path, invocation_info.home, invocation_info.args,
      invocation_info.envs,
      request.Message().command_request().working_directory(),
      invocation_info.bin, request.In(), request.Out(), request.Err()));

  const bool should_wait =
      (request.Message().command_request().wait_behavior() !=
       cvd::WAIT_BEHAVIOR_START);
  FireCommand(std::move(command), should_wait);
  if (!should_wait) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }
  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());
  if (infop.si_code != CLD_EXITED || infop.si_status != EXIT_SUCCESS) {
    instance_manager_.RemoveInstanceGroup(uid, invocation_info.home);
  }
  return ResponseFromSiginfo(infop);
}

Result<void> CvdStartCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<bool> CvdStartCommandHandler::UpdateInstanceDatabase(
    const CommandInvocationInfo& invocation_info) {
  auto& envs = invocation_info.envs;
  if (HasHelpOpts(invocation_info.args)) {
    return {false};
  }
  InstanceNumsCalculator calculator;
  auto instance_env = envs.find(cuttlefish::kCuttlefishInstanceEnvVarName);
  if (instance_env != envs.end()) {
    std::int32_t instance_num = -1;
    CF_EXPECT(android::base::ParseInt(instance_env->second, &instance_num));
    calculator.BaseInstanceNum(instance_num);
  }

  // Track this assembly_dir in the fleet.
  InstanceManager::InstanceGroupInfo info;
  info.host_binaries_dir = invocation_info.host_artifacts_path + "/bin/";
  info.instances = CF_EXPECT(calculator.Calculate());
  CF_EXPECT(instance_manager_.SetInstanceGroup(invocation_info.uid,
                                               invocation_info.home, info),
            invocation_info.home
                << " is already taken so can't create new instance.");
  return {true};
}

Result<std::string> CvdStartCommandHandler::MakeBinPathFromDatabase(
    const CommandInvocationInfo& invocation_info) const {
  auto assembly_info = CF_EXPECT(instance_manager_.GetInstanceGroupInfo(
      invocation_info.uid, invocation_info.home));
  return assembly_info.host_binaries_dir + invocation_info.bin;
}

Result<void> CvdStartCommandHandler::FireCommand(Command&& command,
                                                 const bool wait) {
  SubprocessOptions options;
  if (!wait) {
    options.ExitWithParent(false);
  }
  CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));
  return {};
}

bool CvdStartCommandHandler::HasHelpOpts(
    const std::vector<std::string>& args) const {
  std::vector<std::string> copied_args(args);
  std::vector<Flag> flags;
  bool bool_value_placeholder = false;
  std::string str_value_placeholder;
  for (const auto bool_opt : help_bool_opts_) {
    flags.emplace_back(GflagsCompatFlag(bool_opt, bool_value_placeholder));
  }
  for (const auto str_opt : help_str_opts_) {
    flags.emplace_back(GflagsCompatFlag(str_opt, str_value_placeholder));
  }
  ParseFlags(flags, copied_args);
  // if there was any match, some in copied_args were consumed.
  return (args.size() != copied_args.size());
}

const std::map<std::string, std::string>
    CvdStartCommandHandler::command_to_binary_map_ = {
        {"start", kStartBin},
        {"launch_cvd", kStartBin},
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish

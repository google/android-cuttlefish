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

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

Result<bool> CvdStartCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return command_to_binary_map_.find(invocation.command) !=
         command_to_binary_map_.end();
}

Result<cvd::Response> CvdStartCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  if (interrupted_) {
    return CF_ERR("Interrupted");
  }
  CF_EXPECT(CanHandle(request));
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
  auto& envs = invocation_info.envs;

  InstanceNumsCalculator calculator;
  auto instance_env = envs.find("CUTTLEFISH_INSTANCE");
  if (instance_env != envs.end()) {
    calculator.BaseInstanceNum(std::stoi(instance_env->second));
  }

  // Track this assembly_dir in the fleet.
  InstanceManager::InstanceGroupInfo info;
  info.host_binaries_dir = invocation_info.host_artifacts_path + "/bin/";
  info.instances = CF_EXPECT(calculator.Calculate());
  instance_manager_.SetInstanceGroup(invocation_info.home, info);

  auto assembly_info =
      CF_EXPECT(instance_manager_.GetInstanceGroup(invocation_info.home));
  const std::string bin_path =
      assembly_info.host_binaries_dir + invocation_info.bin;

  Command command = CF_EXPECT(ConstructCommand(
      bin_path, invocation_info.home, invocation_info.args, envs,
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
  return UnlockAndWait(interrupt_lock);
}

Result<void> CvdStartCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
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

Result<cvd::Response> CvdStartCommandHandler::UnlockAndWait(
    std::unique_lock<std::mutex>& interrupt_lock) {
  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());
  return ResponseFromSiginfo(infop);
}

const std::map<std::string, std::string>
    CvdStartCommandHandler::command_to_binary_map_ = {
        {"start", kStartBin},
        {"launch_cvd", kStartBin},
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish

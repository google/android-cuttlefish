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

#include "host/commands/cvd/server_command_generic_impl.h"

#include <sys/types.h>

#include <android-base/file.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

Result<bool> CvdCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(command_to_binary_map_, invocation.command);
}

Result<void> CvdCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<cvd::Response> CvdCommandHandler::Handle(
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

  if (invocation_info.bin == kClearBin) {
    *response.mutable_status() =
        instance_manager_.CvdClear(uid, request.Out(), request.Err());
    return response;
  }

  if (invocation_info.bin == kFleetBin) {
    *response.mutable_status() = CF_EXPECT(HandleCvdFleet(
        request, invocation_info.args, invocation_info.host_artifacts_path));
    return response;
  }

  std::string bin_path = invocation_info.bin;
  if (invocation_info.bin != kMkdirBin && invocation_info.bin != kLnBin) {
    auto assembly_info_result =
        instance_manager_.GetInstanceGroupInfo(uid, invocation_info.home);
    if (assembly_info_result.ok()) {
      auto assembly_info = assembly_info_result.value();
      bin_path =
          assembly_info.host_artifacts_path + "/bin/" + invocation_info.bin;
    } else {
      bin_path =
          invocation_info.host_artifacts_path + "/bin/" + invocation_info.bin;
    }
  }

  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = invocation_info.home,
      .args = invocation_info.args,
      .envs = invocation_info.envs,
      .working_dir = request.Message().command_request().working_directory(),
      .command_name = invocation_info.bin,
      .in = request.In(),
      .out = request.Out(),
      .err = request.Err()};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  SubprocessOptions options;
  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    options.ExitWithParent(false);
  }
  CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));

  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());

  if (infop.si_code == CLD_EXITED && invocation_info.bin == kStopBin) {
    instance_manager_.RemoveInstanceGroup(uid, invocation_info.home);
  }

  return ResponseFromSiginfo(infop);
}

Result<cvd::Status> CvdCommandHandler::HandleCvdFleet(
    const RequestWithStdio& request, const std::vector<std::string>& args,
    const std::string& host_artifacts_path) {
  const auto& envs = request.Message().command_request().env();
  std::optional<std::string> config_path = std::nullopt;
  if (Contains(envs, kCuttlefishConfigEnvVarName)) {
    config_path = envs.at(kCuttlefishConfigEnvVarName);
  }
  CF_EXPECT(request.Credentials() != std::nullopt);
  const uid_t uid = request.Credentials()->uid;
  return instance_manager_.CvdFleet(uid, request.Out(), request.Err(),
                                    config_path, host_artifacts_path, args);
}

const std::map<std::string, std::string>
    CvdCommandHandler::command_to_binary_map_ = {
        {"host_bugreport", kHostBugreportBin},
        {"cvd_host_bugreport", kHostBugreportBin},
        {"status", kStatusBin},
        {"cvd_status", kStatusBin},
        {"stop", kStopBin},
        {"stop_cvd", kStopBin},
        {"clear", kClearBin},
        {"mkdir", kMkdirBin},
        {"ln", kLnBin},
        {"fleet", kFleetBin},
        {"display", kDisplayBin},
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish

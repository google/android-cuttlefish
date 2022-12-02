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

#include "host/commands/cvd/server_command_fleet_impl.h"

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

Result<bool> CvdFleetCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return invocation.command == kFleetSubcmd;
}

Result<void> CvdFleetCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

Result<cvd::Response> CvdFleetCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  if (interrupted_) {
    return CF_ERR("Interrupted");
  }
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(request.Credentials() != std::nullopt);

  cvd::Response response;
  response.mutable_command_response();

  auto [sub_cmd, args] = ParseInvocation(request.Message());
  auto envs = ConvertProtoMap(request.Message().command_request().env());
  CF_EXPECT(Contains(envs, "ANDROID_HOST_OUT") &&
            DirectoryExists(envs.at("ANDROID_HOST_OUT")));

  *response.mutable_status() =
      CF_EXPECT(HandleCvdFleet(request, args, envs.at("ANDROID_HOST_OUT")));

  return response;
}

Result<cvd::Status> CvdFleetCommandHandler::HandleCvdFleet(
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

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish

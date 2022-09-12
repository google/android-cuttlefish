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

#include <android-base/file.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

Result<bool> CvdCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return command_to_binary_map_.find(invocation.command) !=
         command_to_binary_map_.end();
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
  cvd::Response response;
  response.mutable_command_response();

  auto invocation = ParseInvocation(request.Message());

  auto subcommand_bin = command_to_binary_map_.find(invocation.command);
  CF_EXPECT(subcommand_bin != command_to_binary_map_.end());
  auto bin = subcommand_bin->second;

  // HOME is used to possibly set CuttlefishConfig path env variable later.
  // This env variable is used by subcommands when locating the config.
  auto request_home = request.Message().command_request().env().find("HOME");
  std::string home =
      request_home != request.Message().command_request().env().end()
          ? request_home->second
          : StringFromEnv("HOME", ".");

  // Create a copy of args before parsing, to be passed to subcommands.
  auto args = invocation.arguments;
  auto args_copy = invocation.arguments;

  auto host_artifacts_path =
      request.Message().command_request().env().find("ANDROID_HOST_OUT");
  if (host_artifacts_path == request.Message().command_request().env().end()) {
    response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
    response.mutable_status()->set_message(
        "Missing ANDROID_HOST_OUT in client environment.");
    return response;
  }

  if (bin == kClearBin) {
    *response.mutable_status() =
        instance_manager_.CvdClear(request.Out(), request.Err());
    return response;
  }

  if (bin == kFleetBin) {
    *response.mutable_status() =
        HandleCvdFleet(request, args, host_artifacts_path->second);
    return response;
  }

  Command command("(replaced)");
  if (bin == kMkdirBin || bin == kLnBin) {
    command.SetExecutableAndName(bin);
  } else {
    auto assembly_info = CF_EXPECT(instance_manager_.GetInstanceGroup(home));
    command.SetExecutableAndName(assembly_info.host_binaries_dir + bin);
  }
  for (const std::string& arg : args_copy) {
    command.AddParameter(arg);
  }

  // Set CuttlefishConfig path based on assembly dir,
  // used by subcommands when locating the CuttlefishConfig.
  if (request.Message().command_request().env().count(
          kCuttlefishConfigEnvVarName) == 0) {
    auto config_path = GetCuttlefishConfigPath(home);
    if (config_path.ok()) {
      command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, *config_path);
    }
  }
  for (auto& it : request.Message().command_request().env()) {
    command.UnsetFromEnvironment(it.first);
    command.AddEnvironmentVariable(it.first, it.second);
  }

  // Redirect stdin, stdout, stderr back to the cvd client
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, request.In());
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, request.Out());
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, request.Err());
  SubprocessOptions options;

  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    options.ExitWithParent(false);
  }

  const auto& working_dir =
      request.Message().command_request().working_directory();
  if (!working_dir.empty()) {
    auto fd = SharedFD::Open(working_dir, O_RDONLY | O_PATH | O_DIRECTORY);
    CF_EXPECT(fd->IsOpen(),
              "Couldn't open \"" << working_dir << "\": " << fd->StrError());
    command.SetWorkingDirectory(fd);
  }

  CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));

  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());

  if (infop.si_code == CLD_EXITED && bin == kStopBin) {
    instance_manager_.RemoveInstanceGroup(home);
  }

  return ResponseFromSiginfo(infop);
}

cvd::Status CvdCommandHandler::HandleCvdFleet(
    const RequestWithStdio& request, const std::vector<std::string>& args,
    const std::string& host_artifacts_path) {
  auto env_config = request.Message().command_request().env().find(
      kCuttlefishConfigEnvVarName);
  std::optional<std::string> config_path = std::nullopt;
  if (env_config != request.Message().command_request().env().end()) {
    config_path = env_config->second;
  }
  return instance_manager_.CvdFleet(request.Out(), request.Err(), config_path,
                                    host_artifacts_path, args);
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
};

}  // namespace cvd_cmd_impl
}  // namespace cuttlefish

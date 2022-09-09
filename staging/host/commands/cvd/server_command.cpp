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

#include "host/commands/cvd/server.h"

#include <set>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command_fetch_impl.h"
#include "host/commands/cvd/server_command_impl.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace cvd_cmd_impl {

class CvdCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdCommandHandler(InstanceManager& instance_manager,
                           SubprocessWaiter& subprocess_waiter))
      : instance_manager_(instance_manager),
        subprocess_waiter_(subprocess_waiter) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return command_to_binary_map_.find(invocation.command) !=
           command_to_binary_map_.end();
  }
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
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
    if (host_artifacts_path ==
        request.Message().command_request().env().end()) {
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

    if (bin == kStartBin) {
      InstanceNumsCalculator calculator;
      auto instance_env =
          request.Message().command_request().env().find("CUTTLEFISH_INSTANCE");
      if (instance_env != request.Message().command_request().env().end()) {
        calculator.BaseInstanceNum(std::stoi(instance_env->second));
      }

      // Track this assembly_dir in the fleet.
      InstanceManager::InstanceGroupInfo info;
      info.host_binaries_dir = host_artifacts_path->second + "/bin/";
      info.instances = CF_EXPECT(calculator.Calculate());
      instance_manager_.SetInstanceGroup(home, info);
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
        command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName,
                                       *config_path);
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
  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    CF_EXPECT(subprocess_waiter_.Interrupt());
    return {};
  }

 private:
  cvd::Status HandleCvdFleet(const RequestWithStdio& request,
                             const std::vector<std::string>& args,
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
  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;

  static constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
  static constexpr char kStartBin[] = "cvd_internal_start";
  static constexpr char kLnBin[] = "ln";
  static constexpr char kMkdirBin[] = "mkdir";

  static constexpr char kClearBin[] =
      "clear_placeholder";  // Unused, runs CvdClear()
  static constexpr char kFleetBin[] =
      "fleet_placeholder";  // Unused, runs CvdFleet()

  static const std::map<std::string, std::string> command_to_binary_map_;
};

const std::map<std::string, std::string>
    CvdCommandHandler::command_to_binary_map_ = {
        {"host_bugreport", kHostBugreportBin},
        {"cvd_host_bugreport", kHostBugreportBin},
        {"start", kStartBin},
        {"launch_cvd", kStartBin},
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

CommandInvocation ParseInvocation(const cvd::Request& request) {
  CommandInvocation invocation;
  if (request.contents_case() != cvd::Request::ContentsCase::kCommandRequest) {
    return invocation;
  }
  if (request.command_request().args_size() == 0) {
    return invocation;
  }
  for (const std::string& arg : request.command_request().args()) {
    invocation.arguments.push_back(arg);
  }
  invocation.arguments[0] = cpp_basename(invocation.arguments[0]);
  if (invocation.arguments[0] == "cvd") {
    if (invocation.arguments.size() == 1) {
      // Show help if user invokes `cvd` alone.
      invocation.command = "help";
      invocation.arguments = {};
    } else {  // More arguments
      invocation.command = invocation.arguments[1];
      invocation.arguments.erase(invocation.arguments.begin());
      invocation.arguments.erase(invocation.arguments.begin());
    }
  } else {
    invocation.command = invocation.arguments[0];
    invocation.arguments.erase(invocation.arguments.begin());
  }
  return invocation;
}

fruit::Component<fruit::Required<InstanceManager>> cvdCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdCommandHandler>()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdFetchHandler>();
}

}  // namespace cuttlefish

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
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/instance_nums.h"

namespace cuttlefish {
namespace {

constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
constexpr char kStartBin[] = "cvd_internal_start";
constexpr char kFetchBin[] = "fetch_cvd";
constexpr char kMkdirBin[] = "/bin/mkdir";

constexpr char kClearBin[] = "clear_placeholder";  // Unused, runs CvdClear()
constexpr char kFleetBin[] = "fleet_placeholder";  // Unused, runs CvdFleet()

const std::map<std::string, std::string> CommandToBinaryMap = {
    {"host_bugreport", kHostBugreportBin},
    {"cvd_host_bugreport", kHostBugreportBin},
    {"start", kStartBin},
    {"launch_cvd", kStartBin},
    {"status", kStatusBin},
    {"cvd_status", kStatusBin},
    {"stop", kStopBin},
    {"stop_cvd", kStopBin},
    {"clear", kClearBin},
    {"fetch", kFetchBin},
    {"fetch_cvd", kFetchBin},
    {"mkdir", kMkdirBin},
    {"fleet", kFleetBin},
};

}  // namespace

CvdCommandHandler::CvdCommandHandler(InstanceManager& instance_manager)
    : instance_manager_(instance_manager) {}

Result<bool> CvdCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return CommandToBinaryMap.find(invocation.command) !=
         CommandToBinaryMap.end();
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

  auto subcommand_bin = CommandToBinaryMap.find(invocation.command);
  CF_EXPECT(subcommand_bin != CommandToBinaryMap.end());
  auto bin = subcommand_bin->second;

  // HOME is used to possibly set CuttlefishConfig path env variable later. This
  // env variable is used by subcommands when locating the config.
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
  } else if (bin == kFleetBin) {
    auto env_config = request.Message().command_request().env().find(
        kCuttlefishConfigEnvVarName);
    std::string config_path;
    if (env_config != request.Message().command_request().env().end()) {
      config_path = env_config->second;
    }
    *response.mutable_status() =
        instance_manager_.CvdFleet(request.Out(), config_path);
    return response;
  } else if (bin == kStartBin) {
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
  if (bin == kFetchBin) {
    command.SetExecutable("/proc/self/exe").SetName("fetch_cvd");
  } else if (bin == kMkdirBin) {
    command.SetExecutableAndName(kMkdirBin);
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
    if (config_path) {
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

  if (!(bin == kMkdirBin || bin == kFetchBin)) {
    const auto& working_dir =
        request.Message().command_request().working_directory();
    if (!working_dir.empty()) {
      auto fd = SharedFD::Open(working_dir, O_RDONLY | O_PATH | O_DIRECTORY);
      CF_EXPECT(fd->IsOpen(),
                "Couldn't open \"" << working_dir << "\": " << fd->StrError());
      command.SetWorkingDirectory(fd);
    }
  }

  subprocess_ = command.Start(options);

  if (request.Message().command_request().wait_behavior() ==
      cvd::WAIT_BEHAVIOR_START) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }
  interrupt_lock.unlock();

  siginfo_t infop{};

  // This blocks until the process exits, but doesn't reap it.
  auto result = subprocess_->Wait(&infop, WEXITED | WNOWAIT);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  interrupt_lock.lock();
  // Perform a reaping wait on the process (which should already have exited).
  result = subprocess_->Wait(&infop, WEXITED);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  // The double wait avoids a race around the kernel reusing pids. Waiting
  // with WNOWAIT won't cause the child process to be reaped, so the kernel
  // won't reuse the pid until the Wait call below, and any kill signals won't
  // reach unexpected processes.

  subprocess_ = {};

  if (infop.si_code == CLD_EXITED && bin == kStopBin) {
    instance_manager_.RemoveInstanceGroup(home);
  }

  if (infop.si_code == CLD_EXITED && infop.si_status == 0) {
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  response.mutable_status()->set_code(cvd::Status::INTERNAL);
  if (infop.si_code == CLD_EXITED) {
    response.mutable_status()->set_message("Exited with code " +
                                           std::to_string(infop.si_status));
  } else if (infop.si_code == CLD_KILLED) {
    response.mutable_status()->set_message("Exited with signal " +
                                           std::to_string(infop.si_status));
  } else {
    response.mutable_status()->set_message("Quit with code " +
                                           std::to_string(infop.si_status));
  }
  return response;
}

Result<void> CvdCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  if (subprocess_) {
    auto stop_result = subprocess_->Stop();
    switch (stop_result) {
      case StopperResult::kStopFailure:
        return CF_ERR("Failed to stop subprocess");
      case StopperResult::kStopCrash:
        return CF_ERR("Stopper caused process to crash");
      case StopperResult::kStopSuccess:
        return {};
      default:
        return CF_ERR("Unknown stop result: " << (uint64_t)stop_result);
    }
  }
  return {};
}

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
      .addMultibinding<CvdServerHandler, CvdCommandHandler>();
}

}  // namespace cuttlefish

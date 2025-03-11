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
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
constexpr char kStartBin[] = "cvd_internal_start";

constexpr char kClearBin[] = "clear_placeholder";  // Unused, runs CvdClear()
constexpr char kFleetBin[] = "fleet_placeholder";  // Unused, runs CvdFleet()
constexpr char kHelpBin[] = "help_placeholder";  // Unused, prints kHelpMessage.
constexpr char kHelpMessage[] = R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd <command> <args>

Commands:
  help                Print this message.
  help <command>      Print help for a command.
  start               Start a device.
  stop                Stop a running device.
  clear               Stop all running devices and delete all instance and assembly directories.
  fleet               View the current fleet status.
  kill-server         Kill the cvd_server background process.
  status              Check and print the state of a running instance.
  host_bugreport      Capture a host bugreport, including configs, logs, and tombstones.

Args:
  <command args>      Each command has its own set of args. See cvd help <command>.
  --clean             If provided, runs cvd kill-server before the requested command.
)";

const std::map<std::string, std::string> CommandToBinaryMap = {
    {"help", kHelpBin},
    {"host_bugreport", kHostBugreportBin},
    {"cvd_host_bugreport", kHostBugreportBin},
    {"start", kStartBin},
    {"launch_cvd", kStartBin},
    {"status", kStatusBin},
    {"cvd_status", kStatusBin},
    {"stop", kStopBin},
    {"stop_cvd", kStopBin},
    {"clear", kClearBin},
    {"fleet", kFleetBin}};

class CvdCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdCommandHandler(CvdServer& server)) : server_(server) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return CommandToBinaryMap.find(invocation.command) !=
           CommandToBinaryMap.end();
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) {
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    response.mutable_command_response();

    auto invocation = ParseInvocation(request.Message());

    auto subcommand_bin = CommandToBinaryMap.find(invocation.command);
    CF_EXPECT(subcommand_bin != CommandToBinaryMap.end());
    auto bin = subcommand_bin->second;

    // assembly_dir is used to possibly set CuttlefishConfig path env variable
    // later. This env variable is used by subcommands when locating the config.
    std::vector<Flag> flags;
    std::string assembly_dir =
        StringFromEnv("HOME", ".") + "/cuttlefish_assembly";
    flags.emplace_back(GflagsCompatFlag("assembly_dir", assembly_dir));

    // Create a copy of args before parsing, to be passed to subcommands.
    auto args = invocation.arguments;
    auto args_copy = invocation.arguments;

    CHECK(ParseFlags(flags, invocation.arguments));

    auto host_artifacts_path =
        request.Message().command_request().env().find("ANDROID_HOST_OUT");
    if (host_artifacts_path ==
        request.Message().command_request().env().end()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Missing ANDROID_HOST_OUT in client environment.");
      return response;
    }

    if (bin == kHelpBin) {
      // Handle `cvd help`
      if (args.empty()) {
        WriteAll(request.Out(), kHelpMessage);
        response.mutable_status()->set_code(cvd::Status::OK);
        return response;
      }

      // Certain commands have no detailed help text.
      std::set<std::string> builtins = {"help", "clear", "kill-server"};
      auto it = CommandToBinaryMap.find(args[0]);
      if (it == CommandToBinaryMap.end() ||
          builtins.find(args[0]) != builtins.end()) {
        WriteAll(request.Out(), kHelpMessage);
        response.mutable_status()->set_code(cvd::Status::OK);
        return response;
      }

      // Handle `cvd help <subcommand>` by calling the subcommand with --help.
      bin = it->second;
      args_copy.push_back("--help");
    } else if (bin == kClearBin) {
      *response.mutable_status() =
          server_.CvdClear(request.Out(), request.Err());
      return response;
    } else if (bin == kFleetBin) {
      auto env_config = request.Message().command_request().env().find(
          kCuttlefishConfigEnvVarName);
      std::string config_path;
      if (env_config != request.Message().command_request().env().end()) {
        config_path = env_config->second;
      }
      *response.mutable_status() = server_.CvdFleet(request.Out(), config_path);
      return response;
    } else if (bin == kStartBin) {
      // Track this assembly_dir in the fleet.
      CvdServer::AssemblyInfo info;
      info.host_binaries_dir = host_artifacts_path->second + "/bin/";
      server_.SetAssembly(assembly_dir, info);
    }

    auto assembly_info = CF_EXPECT(server_.GetAssembly(assembly_dir));
    Command command(assembly_info.host_binaries_dir + bin);
    for (const std::string& arg : args_copy) {
      command.AddParameter(arg);
    }

    // Set CuttlefishConfig path based on assembly dir,
    // used by subcommands when locating the CuttlefishConfig.
    if (request.Message().command_request().env().count(
            kCuttlefishConfigEnvVarName) == 0) {
      auto config_path = GetCuttlefishConfigPath(assembly_dir);
      if (config_path) {
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

  Result<void> Interrupt() override {
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

 private:
  CvdServer& server_;
  std::optional<Subprocess> subprocess_;
  std::mutex interruptible_;
};

}  // namespace

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

fruit::Component<fruit::Required<CvdServer>> cvdCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdCommandHandler>();
}

}  // namespace cuttlefish

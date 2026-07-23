/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/request_context.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "fmt/base.h"
#include "fmt/format.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/bugreport.h"
#include "cuttlefish/host/commands/cvd/cli/commands/cache.h"
#include "cuttlefish/host/commands/cvd/cli/commands/clear.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/create.h"
#include "cuttlefish/host/commands/cvd/cli/commands/display.h"
#include "cuttlefish/host/commands/cvd/cli/commands/env.h"
#include "cuttlefish/host/commands/cvd/cli/commands/fetch.h"
#include "cuttlefish/host/commands/cvd/cli/commands/fleet.h"
#include "cuttlefish/host/commands/cvd/cli/commands/help.h"
#include "cuttlefish/host/commands/cvd/cli/commands/lint.h"
#include "cuttlefish/host/commands/cvd/cli/commands/load_configs.h"
#include "cuttlefish/host/commands/cvd/cli/commands/login.h"
#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/power_btn.h"
#include "cuttlefish/host/commands/cvd/cli/commands/powerwash.h"
#include "cuttlefish/host/commands/cvd/cli/commands/ps.h"
#include "cuttlefish/host/commands/cvd/cli/commands/remove.h"
#include "cuttlefish/host/commands/cvd/cli/commands/reset.h"
#include "cuttlefish/host/commands/cvd/cli/commands/restart.h"
#include "cuttlefish/host/commands/cvd/cli/commands/screen_recording.h"
#include "cuttlefish/host/commands/cvd/cli/commands/setup.h"
#include "cuttlefish/host/commands/cvd/cli/commands/snapshot.h"
#include "cuttlefish/host/commands/cvd/cli/commands/start.h"
#include "cuttlefish/host/commands/cvd/cli/commands/status.h"
#include "cuttlefish/host/commands/cvd/cli/commands/stop.h"
#include "cuttlefish/host/commands/cvd/cli/commands/version.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/lock/instance_lock.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

bool CanHandle(const CvdCommandHandler& handler,
               const CommandRequest& request) {
  return Contains(handler.CmdList(), request.Subcommand());
}

std::vector<std::string> GetPossibleCommands(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  std::vector<std::string> possibilities;
  if (request.Subcommand().empty()) {
    return possibilities;
  }

  for (auto& handler : handlers) {
    for (const std::string& command : handler->CmdList()) {
      if (!command.empty() && command.front() == request.Subcommand().front()) {
        possibilities.push_back(command);
        break;
      }
    }
  }
  return possibilities;
}

bool HandleDeprecatedCommands(const CommandRequest& request) {
  if (request.Subcommand() == "acloud") {
    std::cerr << "If you are seeing this error when you tried to run `acloud` "
                 "in an Android lunch environment, you are likely running into "
                 "an error with an outdated `acloud_translator` symlink.  You "
                 "can verify "
                 "with `where acloud`, and there should be two results.\n\nTo "
                 "fix, run `rm $ANDROID_HOST_OUT/bin/acloud` to remove the "
                 "deprecated `acloud_translator` binary symlink.  `m clean; m` "
                 "should also remove the symlink.\n";
    return true;
  }
  return false;
}

void SuggestAlternativeCommands(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  fmt::print(std::cerr,
             "Unable to find a matching command for \"cvd {}\".\nMaybe there "
             "is a typo?  Run `cvd help` for a list of commands.",
             request.Subcommand());
  const std::vector<std::string> possible_commands =
      GetPossibleCommands(request, handlers);
  std::string addendum;
  if (!possible_commands.empty()) {
    fmt::print(std::cerr, "\n\nDid you mean one of:\n\t{}\n",
               fmt::join(possible_commands, "\n\t"));
  }
}

void HandleNoMatches(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  if (!HandleDeprecatedCommands(request)) {
    SuggestAlternativeCommands(request, handlers);
  }
}

}  //  namespace

RequestContext::RequestContext(InstanceManager& instance_manager,
                               InstanceLockFileManager& lock_file_manager) {
  request_handlers_.emplace_back(std::make_unique<CvdCacheCommandHandler>());

  request_handlers_.emplace_back(
      std::make_unique<CvdCreateCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdDisplayCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdEnvCommandHandler>(instance_manager));
  request_handlers_.emplace_back(std::make_unique<CvdFetchCommandHandler>());
  request_handlers_.emplace_back(
      std::make_unique<CvdFleetCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdClearCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdBugreportCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdStopCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdHelpHandler>(this->request_handlers_));
  request_handlers_.emplace_back(std::make_unique<LintCommandHandler>());
  request_handlers_.emplace_back(
      std::make_unique<LoadConfigsCommand>(instance_manager));
  request_handlers_.emplace_back(std::make_unique<CvdLoginCommand>());
  request_handlers_.emplace_back(
      std::make_unique<CvdDevicePowerBtnCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdDevicePowerwashCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdMonitorCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdDeviceRestartCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<RemoveCvdCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdResetCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<ScreenRecordingCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdSnapshotCommandHandler>(instance_manager));
  request_handlers_.emplace_back(std::make_unique<CvdSetupHandler>());
  request_handlers_.emplace_back(
      std::make_unique<CvdStartCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdStatusCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdPsCommandHandler>(instance_manager));
  request_handlers_.emplace_back(std::make_unique<CvdVersionHandler>());
  request_handlers_.emplace_back(
      std::make_unique<CvdLogsHandler>(instance_manager));
}

Result<CvdCommandHandler*> RequestContext::Handler(
    const CommandRequest& request) {
  return CF_EXPECT(RequestHandler(request, request_handlers_));
}

Result<CvdCommandHandler*> RequestHandler(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  std::vector<CvdCommandHandler*> compatible_handlers;
  for (auto& handler : handlers) {
    if (CanHandle(*handler, request)) {
      compatible_handlers.push_back(handler.get());
    }
  }

  CF_EXPECT_LE(compatible_handlers.size(), 1,
               "The command matched multiple handlers, which should not "
               "happen.  Please open a bug with the Cuttlefish team and "
               "include the exact command that raised the error.");

  if (compatible_handlers.size() != 1) {
    HandleNoMatches(request, handlers);
    return CF_ERRF("Unable to find matching command for \"cvd {}\".",
                   request.Subcommand());
  }
  return compatible_handlers[0];
}

}  // namespace cuttlefish

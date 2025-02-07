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

#include "host/commands/cvd/cli/request_context.h"

#include <memory>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/command_sequence.h"
#include "host/commands/cvd/cli/commands/acloud_command.h"
#include "host/commands/cvd/cli/commands/acloud_mixsuperimage.h"
#include "host/commands/cvd/cli/commands/acloud_translator.h"
#include "host/commands/cvd/cli/commands/bugreport.h"
#include "host/commands/cvd/cli/commands/cache.h"
#include "host/commands/cvd/cli/commands/clear.h"
#include "host/commands/cvd/cli/commands/cmd_list.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/cli/commands/create.h"
#include "host/commands/cvd/cli/commands/display.h"
#include "host/commands/cvd/cli/commands/env.h"
#include "host/commands/cvd/cli/commands/fetch.h"
#include "host/commands/cvd/cli/commands/fleet.h"
#include "host/commands/cvd/cli/commands/help.h"
#include "host/commands/cvd/cli/commands/lint.h"
#include "host/commands/cvd/cli/commands/load_configs.h"
#include "host/commands/cvd/cli/commands/login.h"
#include "host/commands/cvd/cli/commands/noop.h"
#include "host/commands/cvd/cli/commands/power_btn.h"
#include "host/commands/cvd/cli/commands/powerwash.h"
#include "host/commands/cvd/cli/commands/remove.h"
#include "host/commands/cvd/cli/commands/reset.h"
#include "host/commands/cvd/cli/commands/restart.h"
#include "host/commands/cvd/cli/commands/snapshot.h"
#include "host/commands/cvd/cli/commands/start.h"
#include "host/commands/cvd/cli/commands/status.h"
#include "host/commands/cvd/cli/commands/stop.h"
#include "host/commands/cvd/cli/commands/try_acloud.h"
#include "host/commands/cvd/cli/commands/version.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/instances/lock/instance_lock.h"

namespace cuttlefish {

namespace {

std::vector<std::string> GetPossibleCommands(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers) {
  std::vector<std::string> possibilities;
  if (request.Subcommand().empty()) {
    return possibilities;
  }

  for (auto& handler : handlers) {
    for (const std::string& command : handler->CmdList()) {
      if (!command.empty() &&
          android::base::StartsWith(command, request.Subcommand().front())) {
        possibilities.push_back(command);
        break;
      }
    }
  }
  return possibilities;
}

}  //  namespace

RequestContext::RequestContext(InstanceManager& instance_manager,
                               InstanceLockFileManager& lock_file_manager)
    : command_sequence_executor_(this->request_handlers_) {
  request_handlers_.emplace_back(NewAcloudCommand(command_sequence_executor_));
  request_handlers_.emplace_back(NewAcloudMixSuperImageCommand());
  request_handlers_.emplace_back(NewAcloudTranslatorCommand(instance_manager));
  request_handlers_.emplace_back(NewCvdCacheCommandHandler());
  request_handlers_.emplace_back(
      NewCvdCmdlistHandler(command_sequence_executor_));
  request_handlers_.emplace_back(NewCvdCreateCommandHandler(
      instance_manager, command_sequence_executor_, lock_file_manager));
  request_handlers_.emplace_back(NewCvdDisplayCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdEnvCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdFetchCommandHandler());
  request_handlers_.emplace_back(NewCvdFleetCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdClearCommandHandler(instance_manager));
  request_handlers_.emplace_back(
      NewCvdBugreportCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdStopCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdHelpHandler(this->request_handlers_));
  request_handlers_.emplace_back(NewLintCommand());
  request_handlers_.emplace_back(
      NewLoadConfigsCommand(command_sequence_executor_, instance_manager));
  request_handlers_.emplace_back(NewLoginCommand());
  request_handlers_.emplace_back(
      NewCvdDevicePowerBtnCommandHandler(instance_manager));
  request_handlers_.emplace_back(
      NewCvdDevicePowerwashCommandHandler(instance_manager));
  request_handlers_.emplace_back(
      NewCvdDeviceRestartCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewRemoveCvdCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdResetCommandHandler(instance_manager));
  request_handlers_.emplace_back(
      NewCvdSnapshotCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdStartCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewCvdStatusCommandHandler(instance_manager));
  request_handlers_.emplace_back(NewTryAcloudCommand());
  request_handlers_.emplace_back(NewCvdVersionHandler());
  request_handlers_.emplace_back(NewCvdNoopHandler());
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
    if (CF_EXPECT(handler->CanHandle(request))) {
      compatible_handlers.push_back(handler.get());
    }
  }

  CF_EXPECT(compatible_handlers.size() < 2,
            "The command matched multiple handlers which should not happen.  "
            "Please open a bug with the cvd/Cuttlefish team and include the "
            "exact command that raised the error so it can be fixed.");

  if (compatible_handlers.size() != 1) {
    const std::vector<std::string> possible_commands =
        GetPossibleCommands(request, handlers);
    std::string addendum;
    if (!possible_commands.empty()) {
      addendum = fmt::format("\n\nDid you mean one of:\n\t{}",
                             fmt::join(possible_commands, "\n\t"));
    }
    return CF_ERRF(
        "Unable to find a matching command for \"cvd {}\".\nMaybe there "
        "is a typo?  Run `cvd help` for a list of commands.{}",
        request.Subcommand(), addendum);
  }
  return compatible_handlers[0];
}

}  // namespace cuttlefish

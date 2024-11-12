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

#include <vector>

#include <android-base/logging.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/command_sequence.h"
#include "host/commands/cvd/instances/instance_lock.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/cli/commands/acloud_command.h"
#include "host/commands/cvd/cli/commands/acloud_mixsuperimage.h"
#include "host/commands/cvd/cli/commands/acloud_translator.h"
#include "host/commands/cvd/cli/commands/bugreport.h"
#include "host/commands/cvd/cli/commands/clear.h"
#include "host/commands/cvd/cli/commands/cmd_list.h"
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
#include "host/commands/cvd/cli/commands/power.h"
#include "host/commands/cvd/cli/commands/remove.h"
#include "host/commands/cvd/cli/commands/reset.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/commands/snapshot.h"
#include "host/commands/cvd/cli/commands/start.h"
#include "host/commands/cvd/cli/commands/status.h"
#include "host/commands/cvd/cli/commands/stop.h"
#include "host/commands/cvd/cli/commands/try_acloud.h"
#include "host/commands/cvd/cli/commands/version.h"

namespace cuttlefish {

RequestContext::RequestContext(
    InstanceLockFileManager& instance_lockfile_manager,
    InstanceManager& instance_manager)
    : instance_lockfile_manager_(instance_lockfile_manager),
      instance_manager_(instance_manager),
      command_sequence_executor_(this->request_handlers_) {
  request_handlers_.emplace_back(NewAcloudCommand(command_sequence_executor_));
  request_handlers_.emplace_back(NewAcloudMixSuperImageCommand());
  request_handlers_.emplace_back(NewAcloudTranslatorCommand(instance_manager_));
  request_handlers_.emplace_back(
      NewCvdCmdlistHandler(command_sequence_executor_));
  request_handlers_.emplace_back(NewCvdCreateCommandHandler(
      instance_manager_, command_sequence_executor_));
  request_handlers_.emplace_back(
      NewCvdDisplayCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdEnvCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdFetchCommandHandler());
  request_handlers_.emplace_back(NewCvdFleetCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdClearCommandHandler(instance_manager_));
  request_handlers_.emplace_back(
      NewCvdBugreportCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdStopCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdHelpHandler(this->request_handlers_));
  request_handlers_.emplace_back(NewLintCommand());
  request_handlers_.emplace_back(
      NewLoadConfigsCommand(command_sequence_executor_, instance_manager_));
  request_handlers_.emplace_back(NewLoginCommand());
  request_handlers_.emplace_back(
      NewCvdDevicePowerCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewRemoveCvdCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdResetCommandHandler(instance_manager_));
  request_handlers_.emplace_back(
      NewCvdSnapshotCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdStartCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewCvdStatusCommandHandler(instance_manager_));
  request_handlers_.emplace_back(NewTryAcloudCommand());
  request_handlers_.emplace_back(NewCvdVersionHandler());
  request_handlers_.emplace_back(NewCvdNoopHandler());
}

Result<CvdServerHandler*> RequestContext::Handler(
    const CommandRequest& request) {
  return RequestHandler(request, request_handlers_);
}

Result<CvdServerHandler*> RequestHandler(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdServerHandler>>& handlers) {
  Result<cvd::Response> response;
  std::vector<CvdServerHandler*> compatible_handlers;
  for (auto& handler : handlers) {
    if (CF_EXPECT(handler->CanHandle(request))) {
      compatible_handlers.push_back(handler.get());
    }
  }
  CF_EXPECT(compatible_handlers.size() == 1,
            "Expected exactly one handler for message, found "
                << compatible_handlers.size());
  return compatible_handlers[0];
}

}  // namespace cuttlefish

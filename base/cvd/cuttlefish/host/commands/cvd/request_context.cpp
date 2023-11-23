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

#include "host/commands/cvd/request_context.h"

#include <atomic>
#include <vector>

#include <android-base/logging.h>

#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/acloud_command.h"
#include "host/commands/cvd/server_command/acloud_mixsuperimage.h"
#include "host/commands/cvd/server_command/acloud_translator.h"
#include "host/commands/cvd/server_command/cmd_list.h"
#include "host/commands/cvd/server_command/display.h"
#include "host/commands/cvd/server_command/env.h"
#include "host/commands/cvd/server_command/fetch.h"
#include "host/commands/cvd/server_command/fleet.h"
#include "host/commands/cvd/server_command/generic.h"
#include "host/commands/cvd/server_command/handler_proxy.h"
#include "host/commands/cvd/server_command/help.h"
#include "host/commands/cvd/server_command/host_tool_target_manager.h"
#include "host/commands/cvd/server_command/load_configs.h"
#include "host/commands/cvd/server_command/power.h"
#include "host/commands/cvd/server_command/reset.h"
#include "host/commands/cvd/server_command/restart.h"
#include "host/commands/cvd/server_command/serial_launch.h"
#include "host/commands/cvd/server_command/serial_preset.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/shutdown.h"
#include "host/commands/cvd/server_command/snapshot.h"
#include "host/commands/cvd/server_command/start.h"
#include "host/commands/cvd/server_command/status.h"
#include "host/commands/cvd/server_command/subcmd.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/try_acloud.h"
#include "host/commands/cvd/server_command/version.h"
#include "host/libs/web/build_api.h"

namespace cuttlefish {

RequestContext::RequestContext(CvdServer& cvd_server,
                               InstanceManager& instance_manager,
                               BuildApi& build_api,
                               HostToolTargetManager& host_tool_target_manager,
                               std::atomic<bool>& acloud_translator_optout)
    : cvd_server_(cvd_server),
      instance_manager_(instance_manager),
      build_api_(build_api),
      host_tool_target_manager_(host_tool_target_manager),
      command_sequence_executor_(this->request_handlers_),
      acloud_translator_optout_(acloud_translator_optout) {
  request_handlers_.emplace_back(NewAcloudCommand(command_sequence_executor_));
  request_handlers_.emplace_back(NewAcloudMixSuperImageCommand());
  request_handlers_.emplace_back(
      NewAcloudTranslatorCommand(acloud_translator_optout_));
  request_handlers_.emplace_back(
      NewCvdCmdlistHandler(command_sequence_executor_));
  request_handlers_.emplace_back(
      NewCvdDisplayCommandHandler(instance_manager_, subprocess_waiter_));
  request_handlers_.emplace_back(
      NewCvdEnvCommandHandler(instance_manager_, subprocess_waiter_));
  request_handlers_.emplace_back(NewCvdFetchCommandHandler(subprocess_waiter_));
  request_handlers_.emplace_back(
      NewCvdFleetCommandHandler(instance_manager_, subprocess_waiter_));
  request_handlers_.emplace_back(NewCvdGenericCommandHandler(
      instance_manager_, subprocess_waiter_, host_tool_target_manager_));
  request_handlers_.emplace_back(
      NewCvdServerHandlerProxy(command_sequence_executor_));
  request_handlers_.emplace_back(NewCvdHelpHandler(command_sequence_executor_));
  request_handlers_.emplace_back(
      NewLoadConfigsCommand(command_sequence_executor_));
  request_handlers_.emplace_back(NewCvdDevicePowerCommandHandler(
      host_tool_target_manager_, instance_manager_, subprocess_waiter_));
  request_handlers_.emplace_back(NewCvdResetCommandHandler());
  request_handlers_.emplace_back(
      NewCvdRestartHandler(build_api_, cvd_server_, instance_manager_));
  request_handlers_.emplace_back(
      NewSerialLaunchCommand(command_sequence_executor_, lock_file_manager_));
  request_handlers_.emplace_back(NewSerialPreset(command_sequence_executor_));
  request_handlers_.emplace_back(
      NewCvdShutdownHandler(cvd_server_, instance_manager_));
  request_handlers_.emplace_back(NewCvdSnapshotCommandHandler(
      instance_manager_, subprocess_waiter_, host_tool_target_manager_));
  request_handlers_.emplace_back(
      NewCvdStartCommandHandler(instance_manager_, host_tool_target_manager_,
                                command_sequence_executor_));
  request_handlers_.emplace_back(
      NewCvdStatusCommandHandler(instance_manager_, host_tool_target_manager_));
  request_handlers_.emplace_back(
      NewTryAcloudCommand(acloud_translator_optout_));
  request_handlers_.emplace_back(NewCvdVersionHandler());
}

Result<CvdServerHandler*> RequestContext::Handler(
    const RequestWithStdio& request) {
  return RequestHandler(request, request_handlers_);
}

Result<CvdServerHandler*> RequestHandler(
    const RequestWithStdio& request,
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

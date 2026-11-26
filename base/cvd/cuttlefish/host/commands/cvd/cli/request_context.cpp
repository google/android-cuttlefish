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

#include <memory>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/commands/bugreport.h"
#include "cuttlefish/host/commands/cvd/cli/commands/cache.h"
#include "cuttlefish/host/commands/cvd/cli/commands/clear.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/commands/create.h"
#include "cuttlefish/host/commands/cvd/cli/commands/display.h"
#include "cuttlefish/host/commands/cvd/cli/commands/env.h"
#include "cuttlefish/host/commands/cvd/cli/commands/event_devices.h"
#include "cuttlefish/host/commands/cvd/cli/commands/fetch.h"
#include "cuttlefish/host/commands/cvd/cli/commands/fleet.h"
#include "cuttlefish/host/commands/cvd/cli/commands/help.h"
#include "cuttlefish/host/commands/cvd/cli/commands/lint.h"
#include "cuttlefish/host/commands/cvd/cli/commands/load_configs.h"
#include "cuttlefish/host/commands/cvd/cli/commands/login.h"
#include "cuttlefish/host/commands/cvd/cli/commands/logs.h"
#ifdef __linux__
#include "cuttlefish/host/commands/cvd/cli/commands/monitor/command_handler.h"
#endif
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

namespace cuttlefish {

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
  request_handlers_.emplace_back(std::make_unique<CvdHelpHandler>(*this));
  request_handlers_.emplace_back(
      std::make_unique<CvdInputDevicesHandler>(instance_manager));
  request_handlers_.emplace_back(std::make_unique<LintCommandHandler>());
  request_handlers_.emplace_back(
      std::make_unique<LoadConfigsCommand>(instance_manager));
  request_handlers_.emplace_back(std::make_unique<CvdLoginCommand>());
  request_handlers_.emplace_back(
      std::make_unique<CvdDevicePowerBtnCommandHandler>(instance_manager));
  request_handlers_.emplace_back(
      std::make_unique<CvdDevicePowerwashCommandHandler>(instance_manager));
#ifdef __linux__
  request_handlers_.emplace_back(
      std::make_unique<CvdMonitorCommandHandler>(instance_manager));
#endif
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

const std::vector<std::unique_ptr<CvdCommandHandler>>&
RequestContext::Handlers() const {
  return request_handlers_;
}

}  // namespace cuttlefish

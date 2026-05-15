/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"

namespace cuttlefish {

class ScreenRecordingCommandHandler : public CvdCommandHandler {
 public:
  ScreenRecordingCommandHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override;

  std::string SummaryHelp() const override;
  bool RequiresDeviceExists() const override;
  Result<std::string> DetailedHelp(const CommandRequest& request) override;

 private:
  Result<std::pair<LocalInstanceGroup, std::vector<LocalInstance>>>
  SelectInstances(const CommandRequest& request);

  InstanceManager& instance_manager_;
};

std::unique_ptr<CvdCommandHandler> NewScreenRecordingCommandHandler(
    InstanceManager& instance_manager);

}  // namespace cuttlefish

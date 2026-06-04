/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include <string>

#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"

namespace cuttlefish {

class InstanceManager;

class CvdClearCommandHandler : public CvdCommandHandler {
 public:
  CvdClearCommandHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override;

  bool RequiresDeviceExists() const override { return true; }

  std::string SummaryHelp() const override;
  Result<std::string> DetailedHelp(const CommandRequest& request) override;

 private:
  InstanceManager& instance_manager_;
};

std::unique_ptr<CvdCommandHandler> NewCvdClearCommandHandler(
    InstanceManager& instance_manager);

}  // namespace cuttlefish

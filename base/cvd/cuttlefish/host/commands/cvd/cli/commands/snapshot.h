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

#pragma once

#include <memory>
#include <string>

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class CommandRequest;

class CvdSnapshotCommandHandler : public CvdCommandHandler {
 public:
  CvdSnapshotCommandHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  cvd_common::Args CmdList() const override;

  std::string SummaryHelp() const override;
  bool RequiresDeviceExists() const override { return true; }
  Result<std::string> DetailedHelp(
      const CommandRequest& request) const override;

 private:
  Result<Command> GenerateCommand(const CommandRequest& request,
                                  const std::string& subcmd,
                                  cvd_common::Args& subcmd_args,
                                  cvd_common::Envs envs);

  InstanceManager& instance_manager_;
};

std::unique_ptr<CvdCommandHandler> NewCvdSnapshotCommandHandler(
    InstanceManager& instance_manager);

}  // namespace cuttlefish

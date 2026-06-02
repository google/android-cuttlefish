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
#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/selector/num_instances_parser.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class CvdCreateCommandHandler : public CvdCommandHandler {
 public:
  CvdCreateCommandHandler(InstanceManager& instance_manager);

  Result<void> Handle(const CommandRequest& request) override;
  std::vector<std::string> CmdList() const override { return {"create"}; }
  std::string SummaryHelp() const override;
  Result<std::string> DetailedHelp(const CommandRequest&) override;

 private:
  struct CreateFlags {
    std::string host_path;
    std::string product_path;
    bool start;
    std::string config_file;
  };

  std::vector<Flag> ConfigFileModeFlags();
  std::vector<Flag> FlagModeFlags(const cvd_common::Envs& env,
                                  const selector::SelectorOptions&);
  Result<LocalInstanceGroup> CreateGroup(
      InstanceManager& instance_manager,
      const std::vector<std::string>& subcmd_args, const cvd_common::Envs& envs,
      const CommandRequest& request);

  InstanceManager& instance_manager_;
  CreateFlags own_flags_;
  selector::NumInstancesParser num_instances_parser_;
};

std::unique_ptr<CvdCommandHandler> NewCvdCreateCommandHandler(
    InstanceManager& instance_manager);

}  // namespace cuttlefish

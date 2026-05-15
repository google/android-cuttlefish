/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class CvdBugCommandHandler : public CvdCommandHandler {
 public:
  CvdBugCommandHandler(const InstanceDatabase& instance_db);

  Result<void> Handle(const CommandRequest& request) override;
  std::vector<std::string> CmdList() const override;
  std::string SummaryHelp() const override;
  Result<std::string> DetailedHelp(const CommandRequest& request) override;

 private:
  const InstanceDatabase& instance_db_;
};

std::unique_ptr<CvdCommandHandler> NewCvdBugCommandHandler(
    const InstanceDatabase& instance_db);

}  // namespace cuttlefish

//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <ostream>
#include <vector>

#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/commands/command_handler.h"

namespace cuttlefish {

class CommandSequenceExecutor {
 public:
  CommandSequenceExecutor(
      const std::vector<std::unique_ptr<CvdCommandHandler>>& server_handlers);

  Result<void> Execute(const std::vector<CommandRequest>&,
                       std::ostream& report);
  Result<void> ExecuteOne(const CommandRequest&, std::ostream& report);

  std::vector<std::string> CmdList() const;
  Result<CvdCommandHandler*> GetHandler(const CommandRequest& request);

 private:
  const std::vector<std::unique_ptr<CvdCommandHandler>>& server_handlers_;
  std::vector<CvdCommandHandler*> handler_stack_;
};
}  // namespace cuttlefish

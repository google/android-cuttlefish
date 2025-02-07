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
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/command_sequence.h"
#include "host/commands/cvd/cli/commands/command_handler.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/instances/lock/instance_lock.h"

namespace cuttlefish {

class RequestContext {
 public:
  RequestContext(InstanceManager& instance_manager, InstanceLockFileManager&);

  Result<CvdCommandHandler*> Handler(const CommandRequest& request);

 private:
  std::vector<std::unique_ptr<CvdCommandHandler>> request_handlers_;
  CommandSequenceExecutor command_sequence_executor_;
};

Result<CvdCommandHandler*> RequestHandler(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdCommandHandler>>& handlers);

}  // namespace cuttlefish

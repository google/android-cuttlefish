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

#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/cli/command_sequence.h"
#include "host/commands/cvd/instances/instance_lock.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/cli/commands/server_handler.h"

namespace cuttlefish {

class RequestContext {
 public:
  RequestContext(InstanceLockFileManager& instance_lockfile_manager,
                 InstanceManager& instance_manager);

  Result<CvdServerHandler*> Handler(const CommandRequest& request);

 private:
  void InstantiateHandlers();

  std::vector<std::unique_ptr<CvdServerHandler>> request_handlers_;
  InstanceLockFileManager& instance_lockfile_manager_;
  InstanceManager& instance_manager_;
  InstanceLockFileManager lock_file_manager_;
  CommandSequenceExecutor command_sequence_executor_;
};

Result<CvdServerHandler*> RequestHandler(
    const CommandRequest& request,
    const std::vector<std::unique_ptr<CvdServerHandler>>& handlers);

}  // namespace cuttlefish

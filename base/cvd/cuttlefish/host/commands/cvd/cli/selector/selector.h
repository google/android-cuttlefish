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

#include <utility>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"
#include "host/commands/cvd/instances/instance_group_record.h"
#include "host/commands/cvd/instances/instance_manager.h"
#include "host/commands/cvd/instances/instance_record.h"

namespace cuttlefish {
namespace selector {

// Selects a single group based on the request's selector options. Asks the user
// to manually choose a single group if multiple groups match the selector
// options and stdin is a terminal.
Result<LocalInstanceGroup> SelectGroup(const InstanceManager& instance_manager,
                                       const CommandRequest& request);

// Selects a single instance based on the request's selector options. Unlike
// SelectGroup it doesn't ask the user to refine the selection in case multiple
// instances match, it just fails instead. Also returns the group the selected
// instance belongs to.
Result<std::pair<LocalInstance, LocalInstanceGroup>> SelectInstance(
    const InstanceManager& instance_manager, const CommandRequest& request);

}  // namespace selector
}  // namespace cuttlefish

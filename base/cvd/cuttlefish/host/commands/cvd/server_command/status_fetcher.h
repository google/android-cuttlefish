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

#include <sys/types.h>

#include <chrono>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {

// Fetches status from all instances in the group. Waits for the launcher to
// respond for at most timeout seconds.
Result<Json::Value> FetchStatus(
    selector::LocalInstanceGroup& group,
    std::chrono::seconds timeout = std::chrono::seconds(5));

// Fetches status from a single instance. Waits for the launcher to respond
// for at most timeout seconds.
Result<Json::Value> FetchStatus(
    selector::LocalInstance& instance,
    std::chrono::seconds timeout = std::chrono::seconds(5));

}  // namespace cuttlefish

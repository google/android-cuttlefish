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
#include <unistd.h>

#include <vector>

#include "common/libs/utils/result.h"

/**
 * @file Utility functions to retrieve information from proc filesystem
 *
 * As of now, the major consumer is cvd.
 */
namespace cuttlefish {

static constexpr char kProcDir[] = "/proc";

// collect all pids whose owner is uid
Result<std::vector<pid_t>> CollectPids(const uid_t uid = getuid());
Result<uid_t> OwnerUid(const pid_t pid);

}  // namespace cuttlefish

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

/**
 * @file Utils shared by device selectors for non-start operations
 *
 */

#include <sys/types.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_database.h"

namespace cuttlefish {
namespace selector {

Result<LocalInstanceGroup> GetDefaultGroup(
    const InstanceDatabase& instance_database);

}  // namespace selector
}  // namespace cuttlefish

/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <string>

#include "common/libs/utils/result.h"

namespace cuttlefish {

// Get disk usage of a path. If this path is a directory, disk usage will
// account for all files under this folder(recursively).
Result<std::size_t> GetDiskUsageBytes(const std::string& path);
Result<std::size_t> GetDiskUsageGigabytes(const std::string& path);

}  // namespace

/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

// [A-Za-z0-9_]+, e.g. 0, tv, my_phone07, etc
// Or, it can include "-" in the middle
// ([A-Za-z0-9_]+[-])*[A-Za-z0-9_]
bool IsValidInstanceName(const std::string& token);

// [A-Za-z_][A-Za-z0-9_]*, e.g. cool_group, cv0_d, cf, etc
// but can't start with [0-9]
bool IsValidGroupName(const std::string& token);

// <valid group name>-<valid instance name>
bool IsValidDeviceName(const std::string& token);

struct DeviceName {
  std::string group_name;
  std::string per_instance_name;
};
Result<DeviceName> BreakDeviceName(const std::string& device_name);

}  // namespace cuttlefish

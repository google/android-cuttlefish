/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include <vector>

#include <gflags/gflags.h>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

template <typename T>
struct FromGflags {
  std::vector<T> values;
  bool is_default;
};

extern template struct FromGflags<bool>;
extern template struct FromGflags<int>;
extern template struct FromGflags<std::string>;

Result<FromGflags<bool>> BoolFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info, const std::string& flag_name);
Result<FromGflags<bool>> BoolFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info, const std::string& flag_name,
    bool default_value);
Result<FromGflags<int>> IntFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info, const std::string& flag_name);
Result<FromGflags<std::string>> StringFromGlobalGflags(
    const gflags::CommandLineFlagInfo& flag_info, const std::string& flag_name);

}  // namespace cuttlefish

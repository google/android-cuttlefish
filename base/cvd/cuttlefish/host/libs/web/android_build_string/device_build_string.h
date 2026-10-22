//
// Copyright (C) 2023 The Android Open Source Project
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

#include <optional>
#include <string>

#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

struct DeviceBuildString {
  std::string branch_or_id;
  std::optional<std::string> target;
  std::optional<std::string> filepath;

  auto operator<=>(const DeviceBuildString&) const = default;
};

Result<DeviceBuildString> ParseDeviceBuildString(
    const std::string& build_string,
    const std::optional<std::string>& filepath);

std::ostream& operator<<(std::ostream& out,
                         const DeviceBuildString& build_string);

}  // namespace cuttlefish

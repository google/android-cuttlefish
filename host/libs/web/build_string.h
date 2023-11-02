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
#include <variant>
#include <vector>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

struct DeviceBuildString {
  std::string branch_or_id;
  std::optional<std::string> target;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream& out,
                         const DeviceBuildString& build_string);
bool operator==(const DeviceBuildString& lhs, const DeviceBuildString& rhs);
bool operator!=(const DeviceBuildString& lhs, const DeviceBuildString& rhs);

struct DirectoryBuildString {
  std::vector<std::string> paths;
  std::string target;
  std::optional<std::string> filepath;
};

std::ostream& operator<<(std::ostream& out,
                         const DirectoryBuildString& build_string);
bool operator==(const DirectoryBuildString& lhs,
                const DirectoryBuildString& rhs);
bool operator!=(const DirectoryBuildString& lhs,
                const DirectoryBuildString& rhs);

using BuildString = std::variant<DeviceBuildString, DirectoryBuildString>;

std::ostream& operator<<(std::ostream& out, const BuildString& build_string);

std::ostream& operator<<(std::ostream& out,
                         const std::optional<BuildString>& build_string);

std::optional<std::string> GetFilepath(const BuildString& build_string);

void SetFilepath(BuildString& build_string, const std::string& value);

Result<BuildString> ParseBuildString(const std::string& build_string);

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<BuildString>& value);
Flag GflagsCompatFlag(const std::string& name,
                      std::vector<std::optional<BuildString>>& value);

}  // namespace cuttlefish

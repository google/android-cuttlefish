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

#include "build_string.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

namespace {

Result<BuildString> ParseDeviceBuildString(const std::string& build_string) {
  std::size_t slash_pos = build_string.find('/');
  std::string branch_or_id = build_string.substr(0, slash_pos);
  auto result =
      DeviceBuildString{.branch_or_id = build_string.substr(0, slash_pos)};
  if (slash_pos != std::string::npos) {
    std::size_t next_slash_pos = build_string.find('/', slash_pos + 1);
    CF_EXPECTF(next_slash_pos == std::string::npos,
               "Build string argument cannot have more than one '/'.  Found at "
               "positions {},{}.",
               slash_pos, next_slash_pos);
    result.target = build_string.substr(slash_pos + 1);
  }
  return result;
}

Result<DirectoryBuildString> ParseDirectoryBuildString(
    const std::string& build_string) {
  DirectoryBuildString result;
  std::vector<std::string> split = android::base::Split(build_string, ":");
  result.target = split.back();
  split.pop_back();
  result.paths = std::move(split);
  return result;
}

}  // namespace

std::ostream& operator<<(std::ostream& out,
                         const DeviceBuildString& build_string) {
  fmt::print(out, "(branch_or_id=\"{}\", target=\"{}\")",
             build_string.branch_or_id, build_string.target.value_or(""));
  return out;
}

bool operator==(const DeviceBuildString& lhs, const DeviceBuildString& rhs) {
  return lhs.branch_or_id == rhs.branch_or_id && lhs.target == rhs.target;
}

bool operator!=(const DeviceBuildString& lhs, const DeviceBuildString& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& out,
                         const DirectoryBuildString& build_string) {
  fmt::print(out, "(paths=\"{}\", target=\"{}\")",
             fmt::join(build_string.paths, ":"), build_string.target);
  return out;
}

bool operator==(const DirectoryBuildString& lhs,
                const DirectoryBuildString& rhs) {
  return lhs.paths == rhs.paths && lhs.target == rhs.target;
}

bool operator!=(const DirectoryBuildString& lhs,
                const DirectoryBuildString& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& out, const BuildString& build_string) {
  std::visit([&out](auto&& arg) { out << arg; }, build_string);
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const std::optional<BuildString>& build_string) {
  if (build_string) {
    fmt::print(out, "has_value({})", *build_string);
  } else {
    out << "no_value()";
  }
  return out;
}

Result<BuildString> ParseBuildString(const std::string& build_string) {
  CF_EXPECT(!build_string.empty(), "The given build string cannot be empty");
  if (build_string.find(':') != std::string::npos) {
    return CF_EXPECT(ParseDirectoryBuildString(build_string));
  } else {
    return CF_EXPECT(ParseDeviceBuildString(build_string));
  }
}

Flag GflagsCompatFlag(const std::string& name,
                      std::vector<std::optional<BuildString>>& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return android::base::Join(value, ','); })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        if (match.value.empty()) {
          value.clear();
          return {};
        }
        std::vector<std::string> str_vals =
            android::base::Split(match.value, ",");
        value.clear();
        for (const auto& str_val : str_vals) {
          if (str_val.empty()) {
            value.emplace_back(std::nullopt);
          } else {
            value.emplace_back(CF_EXPECT(ParseBuildString(str_val)));
          }
        }
        return {};
      });
}

}  // namespace cuttlefish

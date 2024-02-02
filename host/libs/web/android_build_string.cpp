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

#include "host/libs/web/android_build_string.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

namespace {

Result<std::pair<std::string, std::optional<std::string>>> ParseFilepath(
    const std::string& build_string) {
  std::string remaining_build_string = build_string;
  std::optional<std::string> filepath;
  std::size_t open_bracket = build_string.find('{');
  std::size_t close_bracket = build_string.find('}');

  bool has_open = open_bracket != std::string::npos;
  bool has_close = close_bracket != std::string::npos;
  CF_EXPECTF(
      has_open == has_close,
      "Open or close curly bracket exists without its complement in \"{}\"",
      build_string);
  if (has_open && has_close) {
    std::string remaining_substring = build_string.substr(0, open_bracket);
    CF_EXPECTF(
        !remaining_substring.empty(),
        "The build string excluding filepath cannot be empty.  Input: {}",
        build_string);
    std::size_t filepath_start = open_bracket + 1;
    std::string filepath_substring =
        build_string.substr(filepath_start, close_bracket - filepath_start);
    CF_EXPECTF(
        !filepath_substring.empty(),
        "The filepath between positions {},{} cannot be empty.  Input: {}",
        filepath_start, close_bracket, build_string);
    remaining_build_string = remaining_substring;
    filepath = filepath_substring;
  }
  return {{remaining_build_string, filepath}};
}

Result<BuildString> ParseDeviceBuildString(
    const std::string& build_string,
    const std::optional<std::string>& filepath) {
  std::size_t slash_pos = build_string.find('/');
  std::string branch_or_id = build_string.substr(0, slash_pos);
  auto result = DeviceBuildString{
      .branch_or_id = build_string.substr(0, slash_pos), .filepath = filepath};
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
    const std::string& build_string,
    const std::optional<std::string>& filepath) {
  auto result = DirectoryBuildString{.filepath = filepath};
  std::vector<std::string> split = android::base::Split(build_string, ":");
  result.target = split.back();
  split.pop_back();
  result.paths = std::move(split);
  return result;
}

}  // namespace

std::ostream& operator<<(std::ostream& out,
                         const DeviceBuildString& build_string) {
  fmt::print(out, "(branch_or_id=\"{}\", target=\"{}\", filepath=\"{}\")",
             build_string.branch_or_id, build_string.target.value_or(""),
             build_string.filepath.value_or(""));
  return out;
}

bool operator==(const DeviceBuildString& lhs, const DeviceBuildString& rhs) {
  return lhs.branch_or_id == rhs.branch_or_id && lhs.target == rhs.target &&
         lhs.filepath == rhs.filepath;
}

bool operator!=(const DeviceBuildString& lhs, const DeviceBuildString& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& out,
                         const DirectoryBuildString& build_string) {
  fmt::print(out, "(paths=\"{}\", target=\"{}\", filepath=\"{}\")",
             fmt::join(build_string.paths, ":"), build_string.target,
             build_string.filepath.value_or(""));
  return out;
}

bool operator==(const DirectoryBuildString& lhs,
                const DirectoryBuildString& rhs) {
  return lhs.paths == rhs.paths && lhs.target == rhs.target &&
         lhs.filepath == rhs.filepath;
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
    out << "has_value(" << *build_string << ")";
  } else {
    out << "no_value()";
  }
  return out;
}

std::optional<std::string> GetFilepath(const BuildString& build_string) {
  return std::visit([](auto&& arg) { return arg.filepath; }, build_string);
}

void SetFilepath(BuildString& build_string, const std::string& value) {
  std::visit([&value](auto&& arg) { arg.filepath = value; }, build_string);
}

Result<BuildString> ParseBuildString(const std::string& build_string) {
  CF_EXPECT(!build_string.empty(), "The given build string cannot be empty");
  auto [remaining_build_string, filepath] =
      CF_EXPECT(ParseFilepath(build_string));
  if (remaining_build_string.find(':') != std::string::npos) {
    return CF_EXPECT(
        ParseDirectoryBuildString(remaining_build_string, filepath));
  } else {
    return CF_EXPECT(ParseDeviceBuildString(remaining_build_string, filepath));
  }
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<BuildString>& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() {
        std::stringstream result;
        result << value;
        return result.str();
      })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        value = std::nullopt;
        if (!match.value.empty()) {
          value = CF_EXPECT(ParseBuildString(match.value));
        }
        return {};
      });
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

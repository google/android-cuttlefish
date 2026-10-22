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

#include "cuttlefish/host/libs/web/android_build_string/device_build_string.h"

#include <stddef.h>

#include <optional>
#include <ostream>
#include <string>

#include "fmt/ostream.h"

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<DeviceBuildString> ParseDeviceBuildString(
    const std::string& build_string,
    const std::optional<std::string>& filepath) {
  size_t slash_pos = build_string.find('/');
  std::string branch_or_id = build_string.substr(0, slash_pos);
  auto result = DeviceBuildString{
      .branch_or_id = build_string.substr(0, slash_pos), .filepath = filepath};
  if (slash_pos != std::string::npos) {
    size_t next_slash_pos = build_string.find('/', slash_pos + 1);
    CF_EXPECTF(next_slash_pos == std::string::npos,
               "Build string argument cannot have more than one '/'.  Found at "
               "positions {},{}.",
               slash_pos, next_slash_pos);
    result.target = build_string.substr(slash_pos + 1);
  }
  return result;
}

std::ostream& operator<<(std::ostream& out,
                         const DeviceBuildString& build_string) {
  fmt::print(out, "(branch_or_id=\"{}\", target=\"{}\", filepath=\"{}\")",
             build_string.branch_or_id, build_string.target.value_or(""),
             build_string.filepath.value_or(""));
  return out;
}

}  // namespace cuttlefish

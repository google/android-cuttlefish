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

#include "cuttlefish/host/libs/web/android_build_string/directory_build_string.h"

#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

namespace cuttlefish {

DirectoryBuildString ParseDirectoryBuildString(
    const std::string& build_string,
    const std::optional<std::string>& filepath) {
  auto result = DirectoryBuildString{.filepath = filepath};
  std::vector<std::string> split = absl::StrSplit(build_string, ':');
  result.target = split.back();
  split.pop_back();
  result.paths = std::move(split);
  return result;
}

std::ostream& operator<<(std::ostream& out,
                         const DirectoryBuildString& build_string) {
  fmt::print(out, "(paths=\"{}\", target=\"{}\", filepath=\"{}\")",
             fmt::join(build_string.paths, ":"), build_string.target,
             build_string.filepath.value_or(""));
  return out;
}

}  // namespace cuttlefish

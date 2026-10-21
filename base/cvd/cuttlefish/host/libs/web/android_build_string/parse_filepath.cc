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

#include "cuttlefish/host/libs/web/android_build_string/parse_filepath.h"

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::pair<std::string, std::optional<std::string>>> ParseFilepath(
    std::string_view build_string) {
  std::string_view remaining_build_string = build_string;
  std::optional<std::string> filepath;
  size_t open_bracket = build_string.find('{');
  size_t close_bracket = build_string.find('}');

  bool has_open = open_bracket != std::string::npos;
  bool has_close = close_bracket != std::string::npos;
  CF_EXPECTF(
      has_open == has_close,
      "Open or close curly bracket exists without its complement in \"{}\"",
      build_string);
  if (has_open && has_close) {
    std::string_view remaining_substring = build_string.substr(0, open_bracket);
    CF_EXPECTF(
        !remaining_substring.empty(),
        "The build string excluding filepath cannot be empty.  Input: {}",
        build_string);
    size_t filepath_start = open_bracket + 1;
    std::string_view filepath_substring =
        build_string.substr(filepath_start, close_bracket - filepath_start);
    CF_EXPECTF(
        !filepath_substring.empty(),
        "The filepath between positions {},{} cannot be empty.  Input: {}",
        filepath_start, close_bracket, build_string);
    remaining_build_string = remaining_substring;
    filepath = filepath_substring;
  }
  return {{std::string(remaining_build_string), filepath}};
}

}  // namespace cuttlefish

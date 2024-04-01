//
// Copyright (C) 2024 The Android Open Source Project
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

#include "host/libs/web/chrome_os_build_string.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <android-base/strings.h>
#include <fmt/format.h>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

std::ostream& operator<<(std::ostream& stream, const ChromeOsBuilder& cob) {
  fmt::print(stream, "{}", cob);
  return stream;
}

static Result<ChromeOsBuildString> ParseChromeOsBuildString(
    const std::string& build_string) {
  std::vector<std::string> fragments =
      android::base::Tokenize(build_string, "/");
  if (fragments.size() == 1) {
    return fragments[0];
  } else if (fragments.size() == 3) {
    return ChromeOsBuilder{
        .project = fragments[0],
        .bucket = fragments[1],
        .builder = fragments[2],
    };
  }
  return CF_ERRF("Can't parse '{}' as Chrome OS build string", build_string);
}

std::ostream& operator<<(std::ostream& stream, const ChromeOsBuildString& cb) {
  fmt::print(stream, "{}", cb);
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         const std::optional<ChromeOsBuildString>& cb) {
  fmt::print(stream, "{}", cb);
  return stream;
}

Flag GflagsCompatFlag(const std::string& name,
                      std::vector<std::optional<ChromeOsBuildString>>& value) {
  return GflagsCompatFlag(name)
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
            value.emplace_back(CF_EXPECT(ParseChromeOsBuildString(str_val)));
          }
        }
        return {};
      })
      .Getter([&value]() { return fmt::format("{}", fmt::join(value, ",")); });
}

}  // namespace cuttlefish

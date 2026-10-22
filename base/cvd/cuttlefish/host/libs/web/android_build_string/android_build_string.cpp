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

#include "cuttlefish/host/libs/web/android_build_string/android_build_string.h"

#include <stddef.h>

#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/libs/web/android_build_string/device_build_string.h"
#include "cuttlefish/host/libs/web/android_build_string/directory_build_string.h"
#include "cuttlefish/host/libs/web/android_build_string/gcs_build_string.h"
#include "cuttlefish/host/libs/web/android_build_string/parse_filepath.h"
#include "cuttlefish/host/libs/web/android_build_string/url_build_string.h"
#include "cuttlefish/host/libs/web/url_namespace.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

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

Result<BuildString> ParseBuildString(std::string_view build_string) {
  CF_EXPECT(!build_string.empty(), "The given build string cannot be empty");
  // Checked before the ':' of a directory build string, which every URL also
  // contains.
  const std::optional<std::string_view> scheme = UrlScheme(build_string);
  if (scheme) {
    UrlBuildString build =
        CF_EXPECT(ParseUrlBuildString(*scheme, build_string));
    if (*scheme == "gs") {
      return GcsBuildString{
          .url = build.url, .filepath = build.filepath, .sha256 = build.sha256};
    }
    return HttpBuildString{
        .url = build.url, .filepath = build.filepath, .sha256 = build.sha256};
  }
  auto [remaining_build_string, filepath] =
      CF_EXPECT(ParseFilepath(build_string));
  if (remaining_build_string.find(':') != std::string::npos) {
    return ParseDirectoryBuildString(remaining_build_string, filepath);
  } else {
    return CF_EXPECT(ParseDeviceBuildString(remaining_build_string, filepath));
  }
}

Flag GflagsCompatFlag(const std::string& name,
                      std::optional<BuildString>& value) {
  return Flag::StringFlag(name)
      .Getter([&value]() {
        std::stringstream result;
        result << value;
        return result.str();
      })
      .Setter([&value](std::string_view arg) -> Result<void> {
        value = std::nullopt;
        if (!arg.empty()) {
          value = CF_EXPECT(ParseBuildString(arg));
        }
        return {};
      });
}

Flag GflagsCompatFlag(const std::string& name,
                      std::vector<std::optional<BuildString>>& value) {
  return Flag::StringFlag(name)
      .Getter([&value]() {
        return absl::StrJoin(value, ",", absl::StreamFormatter());
      })
      .Setter([&value](std::string_view arg) -> Result<void> {
        if (arg.empty()) {
          value.clear();
          return {};
        }
        std::vector<std::string> str_vals = absl::StrSplit(arg, ',');
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

namespace {
struct WithFallbackTargetVisitor {
  BuildString operator()(DeviceBuildString build_string,
                         const std::string& fallback) {
    if (!build_string.target) {
      build_string.target = std::move(fallback);
    }
    return build_string;
  }

  BuildString operator()(DirectoryBuildString build_string,
                         const std::string&) {
    return build_string;
  }

  BuildString operator()(GcsBuildString build_string, const std::string&) {
    return build_string;
  }

  BuildString operator()(HttpBuildString build_string, const std::string&) {
    return build_string;
  }
};
}  // namespace

BuildString WithFallbackTarget(BuildString build_string, std::string fallback) {
  std::variant<std::string> fallback_var(std::move(fallback));
  return std::visit(WithFallbackTargetVisitor(), build_string, fallback_var);
}

}  // namespace cuttlefish

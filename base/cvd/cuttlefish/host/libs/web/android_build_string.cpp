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

#include "cuttlefish/host/libs/web/android_build_string.h"

#include <stddef.h>

#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

// Returns the "<scheme>" of a "<scheme>://" build string, which is what
// separates a URL build source from a branch, a build id or a directory path.
std::optional<std::string_view> UrlScheme(std::string_view build_string) {
  constexpr std::string_view kSchemeCharacters =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.";
  // The allowed set also holds digits and "+-.", which cannot open a scheme,
  // and an empty scheme has no opening character at all.
  if (build_string.empty() || !absl::ascii_isalpha(build_string.front())) {
    return std::nullopt;
  }
  const size_t separator = build_string.find_first_not_of(kSchemeCharacters);
  if (separator == std::string_view::npos ||
      !build_string.substr(separator).starts_with("://")) {
    return std::nullopt;
  }
  return build_string.substr(0, separator);
}

// Returns the digest of a "#sha256=<64 hex digits>" fragment, or nullopt when
// `fragment` is anything else.
std::optional<std::string_view> Sha256FragmentDigest(
    std::string_view fragment) {
  constexpr size_t kHexDigits = 64;
  if (!absl::ConsumePrefix(&fragment, "#sha256=") ||
      fragment.size() != kHexDigits ||
      fragment.find_first_not_of("0123456789abcdefABCDEF") !=
          std::string_view::npos) {
    return std::nullopt;
  }
  return fragment;
}

// A fragment is a suffix by definition, so a '#' anywhere else is an error
// rather than something silently kept in the URL.
Result<std::pair<std::string_view, std::optional<std::string_view>>>
ParseSha256Fragment(std::string_view build_string) {
  const size_t fragment_start = build_string.find('#');
  if (fragment_start == std::string_view::npos) {
    return {{build_string, std::nullopt}};
  }
  const std::optional<std::string_view> digest =
      Sha256FragmentDigest(build_string.substr(fragment_start));
  CF_EXPECTF(digest.has_value(),
             "Only a trailing '#sha256=<64 hex digits>' fragment is supported "
             "in a URL build string.  Input: '{}'",
             build_string);
  return {{build_string.substr(0, fragment_start), digest}};
}

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

Result<BuildString> ParseDeviceBuildString(
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

Result<DirectoryBuildString> ParseDirectoryBuildString(
    const std::string& build_string,
    const std::optional<std::string>& filepath) {
  auto result = DirectoryBuildString{.filepath = filepath};
  std::vector<std::string> split = absl::StrSplit(build_string, ':');
  result.target = split.back();
  split.pop_back();
  result.paths = std::move(split);
  return result;
}

Result<BuildString> ParseUrlBuildString(std::string_view scheme,
                                        std::string_view build_string) {
  CF_EXPECTF(scheme != "http",
             "Cleartext 'http://' build sources are not supported, use "
             "'https://' instead.  Input: '{}'",
             build_string);
  CF_EXPECTF(scheme == "gs" || scheme == "https",
             "Unsupported URL scheme '{}'.  The supported URL schemes are "
             "'gs://' and 'https://'.  Input: '{}'",
             scheme, build_string);
  // The format reserves ',' because build strings travel in comma separated
  // lists, where a URL holding one would be split into pieces that each parse
  // as some other kind of build string.
  CF_EXPECTF(build_string.find(',') == std::string_view::npos,
             "URL build strings cannot contain a comma, which the format "
             "reserves because build strings travel in comma separated "
             "lists.  Input: '{}'",
             build_string);

  auto [without_fragment, sha256] =
      CF_EXPECT(ParseSha256Fragment(build_string));
  size_t close_bracket = without_fragment.find('}');
  CF_EXPECTF(close_bracket == std::string_view::npos ||
                 close_bracket + 1 == without_fragment.size(),
             "A URL build string cannot have characters after the closing "
             "curly bracket.  Input: '{}'",
             build_string);
  auto [url, filepath] = CF_EXPECT(ParseFilepath(without_fragment));

  const size_t query = url.find('?');
  const bool object_form = !url.substr(0, query).ends_with('/');
  // The directory form resolves an artifact by joining its name onto the URL,
  // which would put the name after the query string. A signed query also
  // authorizes exactly one resource, so it cannot cover a listing and every
  // artifact under the prefix.
  CF_EXPECTF(object_form || query == std::string::npos,
             "Query strings are only supported on URLs naming an object, not "
             "on a '/'-terminated directory.  Input: '{}'",
             build_string);
  CF_EXPECTF(object_form || !sha256.has_value(),
             "'#sha256=' is only supported on URLs naming an object, not on a "
             "'/'-terminated directory.  Input: '{}'",
             build_string);

  std::optional<std::string> digest;
  if (sha256.has_value()) {
    digest = std::string(*sha256);
  }
  if (scheme == "gs") {
    return GcsBuildString{.url = url, .filepath = filepath, .sha256 = digest};
  }
  return HttpBuildString{.url = url, .filepath = filepath, .sha256 = digest};
}

}  // namespace

std::ostream& operator<<(std::ostream& out,
                         const DeviceBuildString& build_string) {
  fmt::print(out, "(branch_or_id=\"{}\", target=\"{}\", filepath=\"{}\")",
             build_string.branch_or_id, build_string.target.value_or(""),
             build_string.filepath.value_or(""));
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const DirectoryBuildString& build_string) {
  fmt::print(out, "(paths=\"{}\", target=\"{}\", filepath=\"{}\")",
             fmt::join(build_string.paths, ":"), build_string.target,
             build_string.filepath.value_or(""));
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const GcsBuildString& build_string) {
  fmt::print(out, "(url=\"{}\", filepath=\"{}\", sha256=\"{}\")",
             ScrubUrl(build_string.url), build_string.filepath.value_or(""),
             build_string.sha256.value_or(""));
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const HttpBuildString& build_string) {
  fmt::print(out, "(url=\"{}\", filepath=\"{}\", sha256=\"{}\")",
             ScrubUrl(build_string.url), build_string.filepath.value_or(""),
             build_string.sha256.value_or(""));
  return out;
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

Result<BuildString> ParseBuildString(std::string_view build_string) {
  CF_EXPECT(!build_string.empty(), "The given build string cannot be empty");
  // Checked before the ':' of a directory build string, which every URL also
  // contains.
  const std::optional<std::string_view> scheme = UrlScheme(build_string);
  if (scheme) {
    return CF_EXPECT(ParseUrlBuildString(*scheme, build_string));
  }
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

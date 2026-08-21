//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/url_namespace.h"

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"
#include "fmt/format.h"

#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Returns whether `name` names the `kind` archive of a build. Android Build
// names archives "<product>-<kind>-<id>.zip"; a republished artifact set drops
// the build id, leaving "<product>-<kind>.zip".
bool NameMatchesZipKind(std::string_view name, BuildZipKind kind) {
  return absl::StrContains(name, fmt::format("-{}-", kind)) ||
         absl::EndsWith(name, fmt::format("-{}.zip", kind));
}

}  // namespace

std::optional<std::string_view> UrlScheme(std::string_view url) {
  constexpr std::string_view kSchemeCharacters =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.";
  // The allowed set also holds digits and "+-.", which cannot open a scheme,
  // and an empty scheme has no opening character at all.
  if (url.empty() || !absl::ascii_isalpha(url.front())) {
    return std::nullopt;
  }
  const size_t separator = url.find_first_not_of(kSchemeCharacters);
  if (separator == std::string_view::npos ||
      !url.substr(separator).starts_with("://")) {
    return std::nullopt;
  }
  return url.substr(0, separator);
}

Result<ParsedUrl> ParseUrl(std::string_view url) {
  const std::optional<std::string_view> scheme = UrlScheme(url);
  CF_EXPECTF(scheme.has_value(), "'{}' is not a URL.", ScrubUrl(url));
  const std::string_view authority_and_path = url.substr(scheme->size() + 3);
  const size_t authority_end = authority_and_path.find('/');
  CF_EXPECTF(authority_end != std::string_view::npos,
             "The URL '{}' has no '/' after its host or bucket.",
             ScrubUrl(url));
  CF_EXPECTF(authority_end != 0, "The URL '{}' has no host or bucket.",
             ScrubUrl(url));

  std::string_view path = authority_and_path.substr(authority_end + 1);
  std::string query;
  const size_t query_start = path.find('?');
  if (query_start != std::string_view::npos) {
    query = std::string(path.substr(query_start + 1));
    path = path.substr(0, query_start);
  }
  return ParsedUrl{
      .authority = std::string(authority_and_path.substr(0, authority_end)),
      .path = std::string(path),
      .query = query,
  };
}

std::optional<std::string> DeriveProduct(std::string_view basename) {
  if (!absl::ConsumeSuffix(&basename, ".zip")) {
    return std::nullopt;
  }
  const size_t infix = basename.find("-img-");
  if (infix != 0 && infix != std::string_view::npos) {
    return std::string(basename.substr(0, infix));
  }
  if (absl::ConsumeSuffix(&basename, "-img") && !basename.empty()) {
    return std::string(basename);
  }
  return std::nullopt;
}

std::string_view format_as(BuildZipKind kind) {
  switch (kind) {
    case BuildZipKind::kImages:
      return "img";
    case BuildZipKind::kTargetFiles:
      return "target_files";
    case BuildZipKind::kOtaTools:
      return "otatools";
  }
}

Result<std::string> ResolveUrlZipName(std::string_view object,
                                      BuildZipKind kind) {
  // A build that is one archive has no other candidate, but its name must not
  // claim to be a different kind of archive. target_files and otatools are the
  // other archive kinds a fetch resolves, so a name claiming one of those
  // fails here rather than downstream.
  CF_EXPECTF(
      NameMatchesZipKind(object, kind) ||
          (kind == BuildZipKind::kImages && absl::EndsWith(object, ".zip") &&
           !NameMatchesZipKind(object, BuildZipKind::kTargetFiles) &&
           !NameMatchesZipKind(object, BuildZipKind::kOtaTools)),
      "The build holds only '{}', which is not a '{}' zip.", object, kind);
  return std::string(object);
}

Result<std::string> ResolveUrlZipName(const std::vector<std::string>& names,
                                      BuildZipKind kind) {
  std::vector<std::string> matches;
  for (const std::string& name : names) {
    if (absl::EndsWith(name, ".zip") && NameMatchesZipKind(name, kind)) {
      matches.push_back(name);
    }
  }
  CF_EXPECTF(matches.size() == 1,
             "Expected one '{}' zip in the build, found {}.  The build "
             "contains [{}].  Name the archive itself in the URL if it is "
             "named some other way.",
             kind, matches.size(), absl::StrJoin(names, ", "));
  return matches.front();
}

}  // namespace cuttlefish

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

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kUrlProduct[] = "url";
constexpr char kImgKind[] = "img";
constexpr char kTargetFilesKind[] = "target_files";
constexpr char kOtatoolsKind[] = "otatools";
constexpr std::string_view kImgSuffix = "-img.zip";

// Android Build names an archive "<product>-<kind>-<id>.zip"; a republished
// artifact set drops the build id.
bool NamesZipKind(std::string_view name, std::string_view kind) {
  return absl::StrContains(name, absl::StrCat("-", kind, "-")) ||
         absl::EndsWith(name, absl::StrCat("-", kind, ".zip"));
}

}  // namespace

Result<ParsedUrl> ParseUrl(std::string_view url) {
  size_t scheme_end = url.find("://");
  CF_EXPECTF(scheme_end != std::string_view::npos, "'{}' is not a URL.",
             ScrubUrl(url));
  std::string_view authority_and_path = url.substr(scheme_end + 3);
  size_t authority_end = authority_and_path.find('/');
  CF_EXPECTF(authority_end != std::string_view::npos,
             "The URL '{}' has no '/' after its host or bucket.",
             ScrubUrl(url));
  CF_EXPECTF(authority_end != 0, "The URL '{}' has no host or bucket.",
             ScrubUrl(url));

  std::string_view path = authority_and_path.substr(authority_end + 1);
  std::string query;
  size_t query_start = path.find('?');
  if (query_start != std::string_view::npos) {
    query = std::string(path.substr(query_start + 1));
    path = path.substr(0, query_start);
  }
  return ParsedUrl{
      .authority = std::string(authority_and_path.substr(0, authority_end)),
      .path = std::string(path),
      .directory_form = path.empty() || path.ends_with('/'),
      .query = query,
  };
}

std::string DeriveProduct(std::string_view basename) {
  if (!absl::EndsWith(basename, ".zip")) {
    return kUrlProduct;
  }
  size_t infix = basename.find("-img-");
  if (infix != 0 && infix != std::string_view::npos) {
    return std::string(basename.substr(0, infix));
  }
  if (absl::EndsWith(basename, kImgSuffix) &&
      basename.size() > kImgSuffix.size()) {
    return std::string(basename.substr(0, basename.size() - kImgSuffix.size()));
  }
  return kUrlProduct;
}

bool IsArchiveMember(const std::optional<std::string>& object,
                     const std::optional<std::string>& filepath,
                     const std::string& artifact_name) {
  return object.has_value() && artifact_name != *object &&
         absl::EndsWith(*object, ".zip") && filepath == artifact_name;
}

Result<std::string> ResolveUrlZipName(const std::vector<std::string>& names,
                                      bool closed,
                                      const std::optional<std::string>& object,
                                      std::string_view kind) {
  if (object.has_value()) {
    if (NamesZipKind(*object, kind)) {
      return *object;
    }
    // A build that is one archive has no other candidate, but its name must
    // not claim to be a different kind of archive.
    CF_EXPECTF(kind == kImgKind && absl::EndsWith(*object, ".zip") &&
                   !NamesZipKind(*object, kTargetFilesKind) &&
                   !NamesZipKind(*object, kOtatoolsKind),
               "The build holds only '{}', which is not a '{}' zip.", *object,
               kind);
    return *object;
  }
  if (!closed) {
    return CF_ERRF(
        "Cannot discover the '{}' zip of a directory that has no listing.  "
        "Name the archive itself in the URL, or use a 'gs://' URL, which "
        "lists its contents.",
        kind);
  }

  std::vector<std::string> matches;
  for (const std::string& name : names) {
    if (absl::EndsWith(name, ".zip") && NamesZipKind(name, kind)) {
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

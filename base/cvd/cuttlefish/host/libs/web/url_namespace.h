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

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Returns the "<scheme>" of a "<scheme>://..." string, or nullopt when it
// does not begin with one. A scheme starts with a letter and continues with
// letters, digits, '+', '-' and '.'.
std::optional<std::string_view> UrlScheme(std::string_view url);

struct ParsedUrl {
  std::string authority;  // bucket or host
  std::string path;       // no leading '/', ends with '/' in directory form
  std::string query;      // without the '?'

  // Returns whether the URL names a prefix rather than a single object. A URL
  // naming just the bucket or the host has an empty path and names a prefix.
  bool IsDirectoryForm() const { return path.empty() || path.ends_with('/'); }
};

Result<ParsedUrl> ParseUrl(std::string_view url);

// Returns the product named by a "<product>-img-<id>.zip" or
// "<product>-img.zip" artifact, or nullopt when the name does not follow the
// Android Build convention.
std::optional<std::string> DeriveProduct(std::string_view basename);

// Returns true when the object form names one archive, so that `{selector}`
// names a member of it rather than a second artifact of the build.
bool IsArchiveMember(const std::optional<std::string>& object,
                     const std::optional<std::string>& filepath,
                     const std::string& artifact_name);

// The kinds of zip archive a fetch resolves by name.
enum class BuildZipKind {
  kImages,
  kTargetFiles,
  kOtaTools,
};

// Returns how `kind` is spelled inside an artifact name, as in the "img" of
// "<product>-img-<id>.zip".
std::string_view format_as(BuildZipKind kind);

// Returns the name of the `kind` zip of a URL build that holds `object` and
// nothing else.
Result<std::string> ResolveUrlZipName(std::string_view object,
                                      BuildZipKind kind);

// Returns the name of the `kind` zip of a URL build whose complete object
// listing is `names`. A name states its kind either as Android Build's
// "-<kind>-" infix or as a "-<kind>.zip" suffix. `{selector}` never names a
// zip, so it is not an input.
Result<std::string> ResolveUrlZipName(const std::vector<std::string>& names,
                                      BuildZipKind kind);

}  // namespace cuttlefish

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

struct ParsedUrl {
  std::string authority;  // bucket or host
  std::string path;       // no leading '/', ends with '/' in directory form
  bool directory_form;
  std::string query;  // without the '?'
};

Result<ParsedUrl> ParseUrl(std::string_view url);

// Returns the product named by a "<product>-img-<id>.zip" or
// "<product>-img.zip" artifact, or "url" when the name does not follow the
// Android Build convention.
std::string DeriveProduct(std::string_view basename);

// Returns true when the object form names one archive, so that `{selector}`
// names a member of it rather than a second artifact of the build.
bool IsArchiveMember(const std::optional<std::string>& object,
                     const std::optional<std::string>& filepath,
                     const std::string& artifact_name);

// Returns the name of the `kind` zip of a URL build. `names` holds the
// contents of a closed namespace and `object` the single object of a build
// that names one. A name states its kind either as Android Build's "-<kind>-"
// infix or as a "-<kind>.zip" suffix. `{selector}` never names a zip, so it is
// not an input.
Result<std::string> ResolveUrlZipName(const std::vector<std::string>& names,
                                      bool closed,
                                      const std::optional<std::string>& object,
                                      std::string_view kind);

}  // namespace cuttlefish

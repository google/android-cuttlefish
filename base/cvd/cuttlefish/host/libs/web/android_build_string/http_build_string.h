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

#pragma once

#include <optional>
#include <string>

namespace cuttlefish {

// Names either a single file or, with a trailing '/', the prefix holding a
// build's artifacts, over `https://`. Any credential travels in the URL
// itself, as in a pre-signed URL.
struct HttpBuildString {
  std::string url;
  std::optional<std::string> filepath;
  std::optional<std::string> sha256;

  auto operator<=>(const HttpBuildString&) const = default;
};

std::ostream& operator<<(std::ostream& out,
                         const HttpBuildString& build_string);

}  // namespace cuttlefish
